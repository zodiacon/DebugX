#include "MainWindow.h"
#include "CodeEditor.h"
#include "CommandView.h"
#include "Dialogs.h"
#include "DisassemblyView.h"
#include "GdbDebugger.h"
#include "ListPanes.h"
#include "MemoryView.h"
#include "OutputView.h"
#include "SymbolPanes.h"
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QPlainTextEdit>
#include <QTreeWidget>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDialog>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>
#include <QSettings>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTextDocument>
#include <QToolBar>
#include <QUrl>

using namespace DebugEngine;

namespace {
	constexpr int LayoutVersion = 1;
	constexpr int MaxRecent = 10;

	QString BreakpointsSettingsKey(const QString& program) {
		return "breakpoints/" + QString::fromLatin1(program.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
	}
}

MainWindow::MainWindow() {
	s_Instance = this;
	setWindowTitle(tr("DebugX"));
	setWindowIcon(style()->standardIcon(QStyle::SP_ComputerIcon));
	setAcceptDrops(true);
	setDockOptions(AnimatedDocks | AllowTabbedDocks | AllowNestedDocks | GroupedDragging);
	setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
	resize(1400, 900);

	m_Documents = new QTabWidget(this);
	m_Documents->setDocumentMode(true);
	m_Documents->setTabsClosable(true);
	m_Documents->setMovable(true);
	setCentralWidget(m_Documents);
	connect(m_Documents, &QTabWidget::tabCloseRequested, this, [this](int index) {
		auto view = dynamic_cast<SourceView*>(m_Documents->widget(index));
		if (!view)
			return;
		m_Sources.erase(view->Path());
		m_Documents->removeTab(index);
		view->deleteLater();
	});

	CreatePanes();
	DefaultLayout();
	CreateActions();
	m_DefaultState = saveState(LayoutVersion);

	m_StatusMessage = new QLabel(this);
	m_StatusState = new QLabel(this);
	m_StatusTarget = new QLabel(this);
	m_StatusEngine = new QLabel(this);
	statusBar()->addWidget(m_StatusMessage, 1);
	statusBar()->addPermanentWidget(m_StatusState);
	statusBar()->addPermanentWidget(m_StatusTarget);
	statusBar()->addPermanentWidget(m_StatusEngine);
	QProcess gdb;
	gdb.start(qEnvironmentVariable("DEBUGX_GDB", "gdb"), { "--version" });
	if (gdb.waitForFinished(3000))
		m_StatusEngine->setText(QString::fromUtf8(gdb.readAllStandardOutput()).section('\n', 0, 0).section(')', -1).trimmed().prepend("GDB "));

	ApplySettings();
	Session().AddObserver(this);
	UpdateUI();
}

MainWindow::~MainWindow() {
	Session().RemoveObserver(this);
	s_Instance = nullptr;
}

//
// Panes and actions
//

QDockWidget* MainWindow::AddDock(QWidget* widget, const QString& title, const QString& name, QKeySequence shortcut) {
	auto dock = new QDockWidget(title, this);
	dock->setObjectName(name);
	dock->setWidget(widget);
	auto toggle = dock->toggleViewAction();
	if (!shortcut.isEmpty()) {
		// (the shortcut shows the pane and puts the focus in it, as Alt+1..0 do in WinDbg)
		auto show = new QAction(title, this);
		show->setShortcut(shortcut);
		connect(show, &QAction::triggered, this, [dock, widget] {
			dock->show();
			dock->raise();
			widget->setFocus();
			if (auto focus = widget->findChild<QWidget*>(QString(), Qt::FindChildrenRecursively); focus && focus->focusPolicy() != Qt::NoFocus)
				focus->setFocus();
		});
		addAction(show);
		toggle->setShortcut(shortcut);
		toggle->setShortcutContext(Qt::WidgetShortcut);		// (the menu shows it; 'show' acts on it)
	}
	m_Actions["view." + name] = toggle;
	return dock;
}

void MainWindow::CreatePanes() {
	m_Command = new CommandView(this);
	m_Documents->addTab(m_Command, tr("Command"));
	m_Documents->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
	m_Documents->tabBar()->setTabButton(0, QTabBar::LeftSide, nullptr);

	m_Output = new OutputView(this);
	m_CallStack = new CallStackView(this);
	m_Registers = new RegistersView(this);
	m_Modules = new ModulesView(this);
	m_Threads = new ThreadsView(this);
	m_Breakpoints = new BreakpointsView(this);
	m_Locals = new LocalsView(this);
	m_Watch = new WatchView(this);
	m_Disassembly = new DisassemblyView(this);
	m_Memory = new MemoryView(this);
	m_ListPanes = { { m_CallStack, "callstack" }, { m_Registers, "registers" }, { m_Modules, "modules" }, { m_Threads, "threads" }, { m_Breakpoints, "breakpoints" } };
}

void MainWindow::DefaultLayout() {
	auto watch = AddDock(m_Watch, tr("Watch"), "watch", QKeySequence(Qt::ALT | Qt::Key_2));
	auto locals = AddDock(m_Locals, tr("Locals"), "locals", QKeySequence(Qt::ALT | Qt::Key_3));
	auto registers = AddDock(m_Registers, tr("Registers"), "registers", QKeySequence(Qt::ALT | Qt::Key_4));
	auto memory = AddDock(m_Memory, tr("Memory"), "memory", QKeySequence(Qt::ALT | Qt::Key_5));
	auto stack = AddDock(m_CallStack, tr("Call Stack"), "callstack", QKeySequence(Qt::ALT | Qt::Key_6));
	auto disassembly = AddDock(m_Disassembly, tr("Disassembly"), "disassembly", QKeySequence(Qt::ALT | Qt::Key_7));
	auto threads = AddDock(m_Threads, tr("Threads"), "threads", QKeySequence(Qt::ALT | Qt::Key_8));
	auto modules = AddDock(m_Modules, tr("Modules"), "modules", QKeySequence(Qt::ALT | Qt::Key_9));
	auto breakpoints = AddDock(m_Breakpoints, tr("Breakpoints"), "breakpoints", QKeySequence(Qt::ALT | Qt::Key_0));
	auto output = AddDock(m_Output, tr("Output"), "output");

	// bottom: Locals/Watch on the left, Call Stack/Breakpoints/Output on the right; right: Disassembly/Registers, Memory, Threads/Modules
	// (the splits first: a dock that is in a tab group already does not split)
	addDockWidget(Qt::BottomDockWidgetArea, locals);
	splitDockWidget(locals, stack, Qt::Horizontal);
	addDockWidget(Qt::RightDockWidgetArea, disassembly);
	splitDockWidget(disassembly, threads, Qt::Vertical);
	tabifyDockWidget(locals, watch);
	tabifyDockWidget(stack, breakpoints);
	tabifyDockWidget(stack, output);
	tabifyDockWidget(disassembly, registers);
	tabifyDockWidget(threads, modules);
	tabifyDockWidget(threads, memory);
	locals->raise();
	stack->raise();
	disassembly->raise();
	threads->raise();
	resizeDocks({ locals, stack }, { 600, 600 }, Qt::Horizontal);
	resizeDocks({ locals }, { 260 }, Qt::Vertical);
	resizeDocks({ disassembly }, { 520 }, Qt::Horizontal);
}

void MainWindow::CreateActions() {
	auto icon = [this](QStyle::StandardPixmap p) { return style()->standardIcon(p); };
	auto make = [this](const QString& key, const QString& text, QKeySequence shortcut, auto handler, QIcon actionIcon = {}) {
		auto action = new QAction(actionIcon, text, this);
		if (!shortcut.isEmpty())
			action->setShortcut(shortcut);
		action->setShortcutContext(Qt::ApplicationShortcut);
		connect(action, &QAction::triggered, this, handler);
		m_Actions[key] = action;
		return action;
	};

	// File
	auto file = menuBar()->addMenu(tr("&File"));
	file->addAction(make("run", tr("&Run Executable..."), QKeySequence(Qt::CTRL | Qt::Key_E), [this] { RunExecutable(); }, icon(QStyle::SP_FileIcon)));
	file->addAction(make("attach", tr("&Attach to Process..."), QKeySequence(Qt::Key_F6), [this] { AttachToProcess(); }, icon(QStyle::SP_DirLinkIcon)));
	file->addAction(make("dump", tr("Open &Core Dump..."), QKeySequence(Qt::CTRL | Qt::Key_D), [this] { OpenDump(); }, icon(QStyle::SP_DialogOpenButton)));
	file->addAction(make("opensource", tr("Open &Source File..."), QKeySequence::Open, [this] {
		auto path = QFileDialog::getOpenFileName(this, tr("Open Source File"), {}, tr("Source files (*.c *.cc *.cpp *.cxx *.h *.hpp *.inl);;All files (*)"));
		if (!path.isEmpty())
			OpenSource(path, true);
	}));
	m_RecentMenu = file->addMenu(tr("Recent &Targets"));
	connect(m_RecentMenu, &QMenu::aboutToShow, this, [this] { BuildRecentMenu(); });
	file->addSeparator();
	file->addAction(make("exit", tr("E&xit"), QKeySequence::Quit, [this] { close(); }));

	// Edit
	auto edit = menuBar()->addMenu(tr("&Edit"));
	edit->addAction(make("find", tr("&Find..."), QKeySequence::Find, [this] { Find(false, false); }));
	edit->addAction(make("findnext", tr("Find &Next"), QKeySequence(Qt::Key_F3), [this] { Find(true, false); }));
	edit->addAction(make("findprev", tr("Find &Previous"), QKeySequence(Qt::SHIFT | Qt::Key_F3), [this] { Find(true, true); }));
	edit->addAction(make("goto", tr("&Go To..."), QKeySequence(Qt::CTRL | Qt::Key_G), [this] {
		if (auto pane = ActivePane(); pane && pane->GoTo())
			return;
		if (auto source = ActiveSource()) {
			bool ok;
			int line = QInputDialog::getInt(this, tr("Go to Line"), tr("Line:"), source->CaretLine() + 1, 1, source->lines(), 1, &ok);
			if (ok)
				source->ShowLine(line - 1);
		}
	}));

	// Debug
	auto debug = menuBar()->addMenu(tr("&Debug"));
	debug->addAction(make("go", tr("&Go / Run"), QKeySequence(Qt::Key_F5), [this] { Go(); }, icon(QStyle::SP_MediaPlay)));
	debug->addAction(make("break", tr("&Break"), QKeySequence(Qt::CTRL | Qt::Key_Pause), [] { Debugger().Break(); }, icon(QStyle::SP_MediaPause)));
	debug->addAction(make("stop", tr("&Stop Debugging"), QKeySequence(Qt::SHIFT | Qt::Key_F5), [] { Debugger().Stop(); }, icon(QStyle::SP_MediaStop)));
	debug->addAction(make("restart", tr("&Restart"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F5), [this] { Restart(); }, icon(QStyle::SP_BrowserReload)));
	debug->addAction(make("detach", tr("&Detach"), {}, [] { Debugger().Detach(); }));
	debug->addSeparator();
	debug->addAction(make("stepinto", tr("Step &Into"), QKeySequence(Qt::Key_F11), [this] { Step(StepKind::Into); }, icon(QStyle::SP_ArrowDown)));
	debug->addAction(make("stepover", tr("Step &Over"), QKeySequence(Qt::Key_F10), [this] { Step(StepKind::Over); }, icon(QStyle::SP_ArrowForward)));
	debug->addAction(make("stepout", tr("Step O&ut"), QKeySequence(Qt::SHIFT | Qt::Key_F11), [this] { Step(StepKind::Out); }, icon(QStyle::SP_ArrowUp)));
	debug->addAction(make("runtocursor", tr("Run to &Cursor"), QKeySequence(Qt::CTRL | Qt::Key_F10), [this] {
		if (auto pane = ActivePane(); pane && pane->RunToCursor())
			return;
		if (auto source = ActiveSource())
			RunToSourceLine(source, source->CaretLine());
	}));
	debug->addAction(make("setnext", tr("Set &Next Statement"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F10), [this] {
		if (auto pane = ActivePane(); pane && pane->SetNextStatement())
			return;
		if (auto source = ActiveSource())
			SetNextStatementToLine(source, source->CaretLine());
	}));
	debug->addAction(make("shownext", tr("Show Ne&xt Statement"), QKeySequence(QKeyCombination(Qt::AltModifier | Qt::KeypadModifier, Qt::Key_Asterisk)), [this] { ShowNextStatement(); }));
	debug->addSeparator();
	debug->addAction(make("togglebp", tr("&Toggle Breakpoint"), QKeySequence(Qt::Key_F9), [this] {
		if (auto pane = ActivePane(); pane && pane->ToggleBreakpoint())
			return;
		if (auto source = ActiveSource())
			ToggleSourceBreakpoint(source, source->CaretLine());
		else
			QApplication::beep();
	}));
	debug->addAction(make("addbp", tr("&Add Breakpoint..."), QKeySequence(Qt::CTRL | Qt::Key_B), [this] { AddBreakpointDialog(); }));
	debug->addAction(make("enablebps", tr("&Enable All Breakpoints"), {}, [this] { SetAllBreakpointsEnabled(true); }));
	debug->addAction(make("disablebps", tr("D&isable All Breakpoints"), {}, [this] { SetAllBreakpointsEnabled(false); }));
	debug->addAction(make("deletebps", tr("De&lete All Breakpoints"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F9), [this] { DeleteAllBreakpoints(); }));
	debug->addSeparator();
	debug->addAction(make("reload", tr("Re&load Symbols"), {}, [] { Debugger().ReloadSymbols(); }));
	debug->addAction(make("savedump", tr("Save &Core Dump..."), {}, [this] { SaveDump(); }));

	// View
	auto view = menuBar()->addMenu(tr("&View"));
	view->addAction(make("view.command", tr("&Command"), QKeySequence(Qt::ALT | Qt::Key_1), [this] {
		m_Documents->setCurrentWidget(m_Command);
		m_Command->FocusInput();
	}));
	for (auto name : { "watch", "locals", "registers", "memory", "callstack", "disassembly", "threads", "modules", "breakpoints", "output" })
		view->addAction(m_Actions[QString("view.") + name]);

	// Window
	auto window = menuBar()->addMenu(tr("&Window"));
	window->addAction(make("resetlayout", tr("&Reset Window Layout"), {}, [this] { restoreState(m_DefaultState, LayoutVersion); }));
	window->addAction(make("savelayout", tr("&Save Layout As..."), {}, [this] {
		bool ok;
		auto name = QInputDialog::getText(this, tr("Save Layout"), tr("Name:"), QLineEdit::Normal, {}, &ok).trimmed();
		if (ok && !name.isEmpty())
			QSettings().setValue("layouts/" + name, saveState(LayoutVersion));
	}));
	m_LayoutsMenu = window->addMenu(tr("&Layouts"));
	connect(m_LayoutsMenu, &QMenu::aboutToShow, this, [this] {
		m_LayoutsMenu->clear();
		QSettings settings;
		settings.beginGroup("layouts");
		for (auto& name : settings.childKeys()) {
			auto state = settings.value(name).toByteArray();
			m_LayoutsMenu->addAction(name, this, [this, state] { restoreState(state, LayoutVersion); });
		}
		if (m_LayoutsMenu->isEmpty())
			m_LayoutsMenu->addAction(tr("(none saved)"))->setEnabled(false);
	});

	// Options
	auto options = menuBar()->addMenu(tr("&Options"));
	auto hex = make("hex", tr("&Hexadecimal Display"), {}, [](bool on) {
		Debugger().SetDisplayRadix(on ? 16 : 10);
		QSettings().setValue("hexDisplay", on);
	});
	hex->setCheckable(true);
	options->addAction(hex);
	auto children = make("children", tr("Debug &Child Processes"), {}, [](bool on) {
		Debugger().SetDebugChildProcesses(on);
		QSettings().setValue("debugChildren", on);
	});
	children->setCheckable(true);
	options->addAction(children);
	auto confirm = make("confirmexit", tr("&Ask Before Exiting While Debugging"), {}, [](bool on) { QSettings().setValue("confirmExit", on); });
	confirm->setCheckable(true);
	options->addAction(confirm);
	options->addSeparator();
	options->addAction(make("paths", tr("&Symbol and Source Paths..."), {}, [this] { EditPaths(); }));
	options->addAction(make("font", tr("Code &Font..."), {}, [this] {
		bool ok;
		auto font = QFontDialog::getFont(&ok, CodeFont(), this, tr("Code Font"), QFontDialog::MonospacedFonts);
		if (ok) {
			SetCodeFont(font);
			ApplyFont();
		}
	}));

	auto help = menuBar()->addMenu(tr("&Help"));
	help->addAction(make("about", tr("&About DebugX"), {}, [this] {
		QMessageBox::about(this, tr("About DebugX"), tr("<b>DebugX</b> for Linux<br>A debugger front end over GDB/MI.<br>%1").arg(m_StatusEngine->text()));
	}));

	auto toolbar = addToolBar(tr("Debug"));
	toolbar->setObjectName("debugToolbar");
	for (auto key : { "run", "attach", "dump" })
		toolbar->addAction(m_Actions[key]);
	toolbar->addSeparator();
	for (auto key : { "go", "break", "stop", "restart" })
		toolbar->addAction(m_Actions[key]);
	toolbar->addSeparator();
	for (auto key : { "stepinto", "stepover", "stepout" })
		toolbar->addAction(m_Actions[key]);
}

void MainWindow::ApplyFont() {
	m_Command->ApplyFont();
	// (the source documents and the Disassembly pane)
	for (auto editor : findChildren<CodeEditor*>())
		editor->ApplyFont();
	for (auto w : findChildren<QWidget*>())
		if (qobject_cast<QTreeWidget*>(w) || (qobject_cast<QPlainTextEdit*>(w) && w != m_Command))
			w->setFont(CodeFont());
}

void MainWindow::ApplySettings() {
	QSettings settings;
	restoreGeometry(settings.value("geometry").toByteArray());
	if (settings.value("layoutVersion").toInt() == LayoutVersion)
		restoreState(settings.value("state").toByteArray(), LayoutVersion);
	for (auto& [pane, name] : m_ListPanes)
		pane->RestoreState(settings.value("columns/" + name).toByteArray());
	m_Watch->SetExpressions(settings.value("watch").toStringList());
	m_Command->SetHistory(settings.value("history").toStringList());
	m_Memory->SetExpression(settings.value("memory", "$sp").toString());
	m_Disassembly->SetShowSource(settings.value("disassemblySource", true).toBool());
	m_Launch = { settings.value("launch/program").toString(), settings.value("launch/arguments").toString(), settings.value("launch/folder").toString() };

	m_Actions["hex"]->setChecked(settings.value("hexDisplay", false).toBool());
	if (m_Actions["hex"]->isChecked())
		Debugger().SetDisplayRadix(16);
	m_Actions["children"]->setChecked(settings.value("debugChildren", false).toBool());
	if (m_Actions["children"]->isChecked())
		Debugger().SetDebugChildProcesses(true);
	m_Actions["confirmexit"]->setChecked(settings.value("confirmExit", true).toBool());
	auto symbols = settings.value("symbolPath").toString();
	if (!symbols.isEmpty())
		Debugger().SetSymbolPath(W(symbols).c_str());
	auto sources = settings.value("sourcePath").toString();
	if (!sources.isEmpty())
		Debugger().SetSourcePath(W(sources).c_str());
}

void MainWindow::SaveSettings() {
	QSettings settings;
	settings.setValue("geometry", saveGeometry());
	settings.setValue("state", saveState(LayoutVersion));
	settings.setValue("layoutVersion", LayoutVersion);
	for (auto& [pane, name] : m_ListPanes)
		settings.setValue("columns/" + name, pane->SaveState());
	settings.setValue("watch", m_Watch->Expressions());
	settings.setValue("history", m_Command->History());
	settings.setValue("memory", m_Memory->Expression());
	settings.setValue("disassemblySource", m_Disassembly->ShowSource());
	settings.setValue("launch/program", m_Launch.Program);
	settings.setValue("launch/arguments", m_Launch.Arguments);
	settings.setValue("launch/folder", m_Launch.Folder);
}

void MainWindow::closeEvent(QCloseEvent* event) {
	auto& s = Session();
	bool live = s.HasTarget() && (s.Target() & TargetType::Live) == TargetType::Live;
	if (live && m_Actions["confirmexit"]->isChecked()) {
		QMessageBox box(QMessageBox::Question, tr("DebugX"), tr("A program is being debugged. Exit anyway?"), QMessageBox::Yes | QMessageBox::No, this);
		box.setInformativeText(tr("A program DebugX started is terminated; one it attached to is let go."));
		auto dontAsk = new QCheckBox(tr("Don't ask again"), &box);
		box.setCheckBox(dontAsk);
		if (box.exec() != QMessageBox::Yes) {
			event->ignore();
			return;
		}
		if (dontAsk->isChecked())
			m_Actions["confirmexit"]->setChecked(false), QSettings().setValue("confirmExit", false);
	}
	SaveSettings();
	event->accept();
}

//
// The state of the UI follows the session
//

void MainWindow::UpdateUI() {
	auto& s = Session();
	bool hasTarget = s.HasTarget(), canRun = s.CanRun(), running = s.IsRunning();
	bool live = hasTarget && (s.Target() & TargetType::Live) == TargetType::Live;
	auto enable = [this](const char* key, bool on) { m_Actions[key]->setEnabled(on); };
	enable("run", !hasTarget);
	enable("attach", !hasTarget);
	enable("dump", !hasTarget);
	enable("go", !running && !s.IsBusy() && (canRun || !hasTarget));
	enable("break", running);
	enable("stop", hasTarget);
	enable("restart", live && !m_Launch.Program.isEmpty());
	enable("detach", live);
	for (auto key : { "stepinto", "stepover", "stepout", "runtocursor", "setnext" })
		enable(key, canRun);
	enable("shownext", hasTarget && s.IsBroken());
	enable("reload", hasTarget && !running);
	enable("savedump", live && canRun);

	m_StatusState->setText(!hasTarget ? tr("No target") : running ? tr("Running") : s.IsBroken() ? tr("Break") : tr("Busy"));
	m_StatusTarget->setText(!hasTarget ? QString() : (s.Target() & TargetType::Dump) == TargetType::Dump ? tr("Core dump") : tr("Live process"));
	UpdateTitle();
}

void MainWindow::UpdateTitle() {
	auto& s = Session();
	QString title;
	if (s.HasTarget() && !m_TargetName.isEmpty())
		title = QStringLiteral("%1 [%2] - ").arg(m_TargetName, s.IsRunning() ? tr("Running") : tr("Break"));
	setWindowTitle(title + tr("DebugX"));
}

void MainWindow::SetStatusMessage(const QString& text) {
	m_StatusMessage->setText(text);
}

void MainWindow::OnSessionChanged() {
	UpdateUI();
}

void MainWindow::OnBroke() {
	UpdateSourceLocation();
	if (!isActiveWindow())
		QApplication::alert(this);
}

void MainWindow::OnContextChanged() {
	UpdateSourceLocation();
}

void MainWindow::OnScopeChanged() {
	UpdateSourceLocation();
}

void MainWindow::OnBreakpointsChanged() {
	UpdateBreakpointMarkers();
	SaveBreakpoints();
}

void MainWindow::OnTargetEnded() {
	for (auto& [_, view] : m_Sources)
		view->SetCurrentLine(-1, false);
	SetStatusMessage(tr("The target has ended"));
	UpdateBreakpointMarkers();
}

void MainWindow::OnException(const ExceptionData& data) {
	SetStatusMessage(tr("Signal %1: %2 at 0x%3").arg(data.Code).arg(Q(data.Name)).arg(data.Address, 0, 16));
}

void MainWindow::ReportFailure(const QString& what, HRESULT hr) {
	auto error = Q(Debugger().LastError());
	SetStatusMessage(QStringLiteral("%1: %2").arg(what, error.isEmpty() ? QStringLiteral("0x%1").arg(static_cast<unsigned>(hr), 8, 16, QLatin1Char('0')) : error));
	QMessageBox::warning(this, tr("DebugX"), QStringLiteral("%1.\n\n%2").arg(what, error));
}

//
// Sessions
//

void MainWindow::RunExecutable() {
	if (Session().HasTarget())
		return;
	RunExecutableDialog dlg(this);
	dlg.SetValues(m_Launch.Program, m_Launch.Arguments, m_Launch.Folder);
	if (dlg.exec() == QDialog::Accepted)
		Launch({ dlg.Program(), dlg.Arguments(), dlg.Folder() });
}

Task<void> MainWindow::Launch(LaunchInfo info) {
	auto alive = m_Lifetime.Watch();
	if (Session().HasTarget())
		co_return;
	// (GDB keeps breakpoints from one run to the next: another program's go)
	if (!m_LastProgram.isEmpty() && m_LastProgram != info.Program) {
		std::vector<BreakpointInfo> old;
		co_await Debugger().EnumBreakpoints(old);
		for (auto& b : old)
			co_await Debugger().RemoveBreakpoint(b.Id);
		if (!*alive)
			co_return;
	}
	SetStatusMessage(tr("Starting %1...").arg(info.Program));
	m_BreakpointsKey.clear();
	auto hr = co_await Debugger().LaunchAndAttach(W(info.Program).c_str(), W(info.Arguments).c_str(), W(info.Folder).c_str());
	if (!*alive)
		co_return;
	if (FAILED(hr)) {
		ReportFailure(tr("%1 could not be started").arg(info.Program), hr);
		co_return;
	}
	m_Launch = info;
	m_LastProgram = info.Program;
	m_TargetName = QFileInfo(info.Program).fileName();
	AddRecent({ "run", info.Program, info.Arguments, info.Folder });
	SetStatusMessage(tr("Started %1").arg(info.Program));
	UpdateUI();
	co_await RestoreBreakpoints(info.Program);
}

void MainWindow::AttachToProcess() {
	if (Session().HasTarget())
		return;
	std::vector<ServerProcess> processes;
	auto task = [](std::vector<ServerProcess>& list) -> Task<void> { co_await Debugger().EnumServerProcesses(list); }(processes);
	// (EnumServerProcesses reads /proc and completes at once)
	AttachDialog dlg(processes, this);
	if (dlg.exec() == QDialog::Accepted && dlg.Pid())
		Attach(dlg.Pid(), dlg.Name());
}

Task<void> MainWindow::Attach(ULONG pid, QString name) {
	auto alive = m_Lifetime.Watch();
	SetStatusMessage(tr("Attaching to %1 (%2)...").arg(name).arg(pid));
	auto program = QFileInfo(QStringLiteral("/proc/%1/exe").arg(pid)).symLinkTarget();
	auto hr = co_await Debugger().AttachToProcess(pid);
	if (!*alive)
		co_return;
	if (FAILED(hr)) {
		ReportFailure(tr("Cannot attach to %1 (%2)").arg(name).arg(pid), hr);
		if (hr == E_ACCESSDENIED)
			QMessageBox::information(this, tr("DebugX"), tr("Attaching to a process that is not a child of the debugger may be forbidden by Yama "
				"(/proc/sys/kernel/yama/ptrace_scope). Run DebugX as root, or: sudo sysctl kernel.yama.ptrace_scope=0"));
		co_return;
	}
	m_TargetName = QStringLiteral("%1 (%2)").arg(name).arg(pid);
	m_LastProgram = program;
	AddRecent({ "attach", name });
	UpdateUI();
	if (!program.isEmpty())
		co_await RestoreBreakpoints(program);
}

void MainWindow::OpenDump() {
	if (Session().HasTarget())
		return;
	auto path = QFileDialog::getOpenFileName(this, tr("Open Core Dump"), {}, tr("Core dumps (core* *.core);;All files (*)"));
	if (!path.isEmpty())
		OpenDumpFile(path);
}

Task<void> MainWindow::OpenDumpFile(QString path) {
	auto alive = m_Lifetime.Watch();
	auto hr = co_await Debugger().OpenDumpFile(W(path).c_str());
	if (!*alive)
		co_return;
	if (FAILED(hr)) {
		ReportFailure(tr("%1 could not be opened").arg(path), hr);
		co_return;
	}
	m_TargetName = QFileInfo(path).fileName();
	m_BreakpointsKey.clear();
	AddRecent({ "dump", path });
	UpdateUI();
}

Task<void> MainWindow::Restart() {
	auto alive = m_Lifetime.Watch();
	if (m_Launch.Program.isEmpty())
		co_return;
	co_await SaveBreakpoints();
	co_await Debugger().Stop();
	if (!*alive)
		co_return;
	co_await Launch(m_Launch);
}

Task<void> MainWindow::Go() {
	auto& s = Session();
	if (!s.HasTarget()) {
		if (!m_Launch.Program.isEmpty() && QFileInfo(m_Launch.Program).isExecutable())
			co_await Launch(m_Launch);
		else
			RunExecutable();
		co_return;
	}
	if (s.CanRun())
		co_await Debugger().Go();
}

Task<void> MainWindow::Step(StepKind kind) {
	if (!Session().CanRun())
		co_return;
	// by source line unless the Disassembly pane is the active one
	bool byLine = ActivePane() != m_Disassembly;
	co_await Debugger().Step(kind, byLine);
}

void MainWindow::RunToAddress(ULONG64 address) {
	if (Session().CanRun())
		Debugger().RunTo(Debugger().AddressLocation(address).c_str());
}

void MainWindow::SetNextStatement(ULONG64 address) {
	SetNextStatementTask(address);
}

Task<void> MainWindow::SetNextStatementTask(ULONG64 address) {
	auto alive = m_Lifetime.Watch();
	if (!Session().CanRun())
		co_return;
	auto hr = co_await Debugger().SetInstructionOffset(address, false);
	if (!*alive)
		co_return;
	if (hr == SetNextOtherFunction) {
		if (QMessageBox::question(this, tr("Set Next Statement"), tr("The address is in another function than the one the thread is in: the stack will not be "
			"what that code expects. Set it anyway?")) != QMessageBox::Yes)
			co_return;
		hr = co_await Debugger().SetInstructionOffset(address, true);
		if (!*alive)
			co_return;
	}
	if (FAILED(hr))
		ReportFailure(tr("The next statement could not be set"), hr);
}

Task<void> MainWindow::SaveDump() {
	auto alive = m_Lifetime.Watch();
	auto path = QFileDialog::getSaveFileName(this, tr("Save Core Dump"), m_TargetName.section(' ', 0, 0) + ".core", tr("Core dumps (*.core);;All files (*)"));
	if (path.isEmpty())
		co_return;
	SetStatusMessage(tr("Writing %1...").arg(path));
	auto hr = co_await Debugger().WriteDump(W(path).c_str(), true);
	if (!*alive)
		co_return;
	if (FAILED(hr))
		ReportFailure(tr("The core dump could not be written"), hr);
	else
		SetStatusMessage(tr("Wrote %1").arg(path));
}

Task<void> MainWindow::EditPaths() {
	auto alive = m_Lifetime.Watch();
	std::wstring symbols, sources;
	co_await Debugger().GetSymbolPaths(symbols, sources);
	if (!*alive)
		co_return;
	PathsDialog dlg(Q(symbols), Q(sources), this);
	if (dlg.exec() != QDialog::Accepted)
		co_return;
	QSettings().setValue("symbolPath", dlg.SymbolPath());
	QSettings().setValue("sourcePath", dlg.SourcePath());
	co_await Debugger().SetSymbolPath(W(dlg.SymbolPath()).c_str());
	co_await Debugger().SetSourcePath(W(dlg.SourcePath()).c_str());
	if (*alive)
		UpdateSourceLocation();
}

void MainWindow::StartFromCommandLine(const QStringList& arguments) {
	if (arguments.isEmpty())
		return;
	if (arguments[0] == "-p" && arguments.size() > 1) {
		Attach(arguments[1].toUInt(), arguments[1]);
		return;
	}
	if (arguments[0] == "-pn" && arguments.size() > 1) {
		OpenRecent({ "attach", arguments[1] });
		return;
	}
	if (arguments[0] == "-z" && arguments.size() > 1) {
		OpenDumpFile(QFileInfo(arguments[1]).absoluteFilePath());
		return;
	}
	QFileInfo program(arguments[0]);
	if (program.isFile() && program.isExecutable())
		Launch({ program.absoluteFilePath(), arguments.mid(1).join(' '), QDir::currentPath() });
	else if (program.isFile())
		OpenDumpFile(program.absoluteFilePath());
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
	if (event->mimeData()->hasUrls())
		event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
	for (auto& url : event->mimeData()->urls()) {
		QFileInfo file(url.toLocalFile());
		if (!file.isFile())
			continue;
		if (file.isExecutable() && !Session().HasTarget()) {
			RunExecutableDialog dlg(this);
			dlg.SetValues(file.absoluteFilePath(), {}, file.absolutePath());
			if (dlg.exec() == QDialog::Accepted)
				Launch({ dlg.Program(), dlg.Arguments(), dlg.Folder() });
		}
		else if ((file.fileName().startsWith("core") || file.suffix() == "core") && !Session().HasTarget())
			OpenDumpFile(file.absoluteFilePath());
		else
			OpenSource(file.absoluteFilePath(), true);
	}
}

//
// Recent targets: "run\tprogram\targuments\tfolder", "attach\tname", "dump\tpath"
//

void MainWindow::AddRecent(const QStringList& entry) {
	QSettings settings;
	auto list = settings.value("recent").toStringList();
	auto line = entry.join('\t');
	list.removeAll(line);
	list.prepend(line);
	while (list.size() > MaxRecent)
		list.removeLast();
	settings.setValue("recent", list);
}

void MainWindow::BuildRecentMenu() {
	m_RecentMenu->clear();
	auto list = QSettings().value("recent").toStringList();
	int n = 1;
	for (auto& line : list) {
		auto entry = line.split('\t');
		if (entry.size() < 2)
			continue;
		QString text = entry[0] == "run" ? tr("Run %1 %2").arg(entry[1], entry.value(2)) : entry[0] == "attach" ? tr("Attach to %1").arg(entry[1]) : tr("Open %1").arg(entry[1]);
		auto action = m_RecentMenu->addAction(QStringLiteral("&%1 %2").arg(n++ % 10).arg(text.trimmed()), this, [this, entry] { OpenRecent(entry); });
		action->setEnabled(!Session().HasTarget());
	}
	if (m_RecentMenu->isEmpty())
		m_RecentMenu->addAction(tr("(none)"))->setEnabled(false);
}

Task<void> MainWindow::OpenRecent(QStringList entry) {
	if (Session().HasTarget() || entry.size() < 2)
		co_return;
	if (entry[0] == "run")
		co_await Launch({ entry[1], entry.value(2), entry.value(3) });
	else if (entry[0] == "dump")
		co_await OpenDumpFile(entry[1]);
	else if (entry[0] == "attach") {
		// the one running process of that name
		std::vector<ServerProcess> processes;
		co_await Debugger().EnumServerProcesses(processes);
		std::vector<ULONG> pids;
		for (auto& p : processes)
			if (Q(p.Name) == entry[1] && p.Pid != static_cast<ULONG>(QCoreApplication::applicationPid()))
				pids.push_back(p.Pid);
		if (pids.size() == 1)
			co_await Attach(pids[0], entry[1]);
		else
			QMessageBox::information(this, tr("DebugX"), pids.empty() ? tr("No process named %1 is running.").arg(entry[1]) :
				tr("%1 processes are named %2: use Attach to Process.").arg(pids.size()).arg(entry[1]));
	}
}

//
// Breakpoints
//

Task<void> MainWindow::SaveBreakpoints() {
	auto alive = m_Lifetime.Watch();
	auto& s = Session();
	if (m_BreakpointsKey.isEmpty() || !s.HasTarget() || s.IsRunning())
		co_return;
	auto key = m_BreakpointsKey;
	std::vector<BreakpointInfo> breakpoints;
	co_await Debugger().EnumBreakpoints(breakpoints);
	// (the session may have ended meanwhile: what is left then is not the user's choice)
	if (!*alive || !Session().HasTarget() || key != m_BreakpointsKey)
		co_return;
	QStringList locations;
	for (auto& b : breakpoints)
		if (!b.IsData && !b.OneShot && !b.Location.empty())
			locations << Q(b.Location) + '\t' + (b.Enabled ? "1" : "0") + '\t' + Q(b.Condition);
	QSettings().setValue(BreakpointsSettingsKey(key), locations);
}

Task<void> MainWindow::RestoreBreakpoints(QString program) {
	auto alive = m_Lifetime.Watch();
	auto saved = QSettings().value(BreakpointsSettingsKey(program)).toStringList();
	std::vector<BreakpointInfo> existing;
	co_await Debugger().EnumBreakpoints(existing);
	if (!*alive)
		co_return;
	std::set<std::wstring> have;
	for (auto& b : existing)
		have.insert(b.Location);
	for (auto& line : saved) {
		auto parts = line.split('\t');
		if (parts.isEmpty() || have.contains(W(parts[0])))
			continue;
		BreakpointSettings settings;
		settings.Expression = W(parts[0]);
		settings.Enabled = parts.value(1, "1") == "1";
		settings.Condition = W(parts.value(2));
		co_await Debugger().AddBreakpoint(settings);
		if (!*alive)
			co_return;
	}
	m_BreakpointsKey = program;
	UpdateBreakpointMarkers();
}

void MainWindow::AddBreakpointDialog() {
	BreakpointDialog dlg(this);
	if (dlg.exec() != QDialog::Accepted)
		return;
	auto settings = dlg.Settings();
	if (settings.Expression.empty())
		return;
	[](MainWindow* self, BreakpointSettings settings) -> Task<void> {
		auto alive = self->m_Lifetime.Watch();
		auto hr = co_await Debugger().AddBreakpoint(settings);
		if (*alive && FAILED(hr))
			self->ReportFailure(tr("The breakpoint could not be set"), hr);
	}(this, settings);
}

void MainWindow::SetAllBreakpointsEnabled(bool enabled) {
	[](bool enabled) -> Task<void> {
		std::vector<BreakpointInfo> breakpoints;
		co_await Debugger().EnumBreakpoints(breakpoints);
		for (auto& b : breakpoints)
			if (b.Enabled != enabled)
				co_await Debugger().SetBreakpointEnabled(b.Id, enabled);
	}(enabled);
}

void MainWindow::DeleteAllBreakpoints() {
	if (QMessageBox::question(this, tr("Delete All Breakpoints"), tr("Delete all breakpoints?")) != QMessageBox::Yes)
		return;
	[]() -> Task<void> {
		std::vector<BreakpointInfo> breakpoints;
		co_await Debugger().EnumBreakpoints(breakpoints);
		for (auto& b : breakpoints)
			co_await Debugger().RemoveBreakpoint(b.Id);
	}();
}

Task<void> MainWindow::UpdateBreakpointMarkers() {
	auto alive = m_Lifetime.Watch();
	std::vector<BreakpointInfo> breakpoints;
	co_await Debugger().EnumBreakpoints(breakpoints);
	if (!*alive)
		co_return;
	std::vector<ULONG64> offsets;
	std::vector<bool> enabled;
	for (auto& b : breakpoints)
		if (!b.IsData && b.Offset) {
			offsets.push_back(b.Offset);
			enabled.push_back(b.Enabled);
		}
	std::vector<SourceLocation> locations;
	if (!offsets.empty())
		co_await Debugger().GetSourceLocations(offsets, locations);
	if (!*alive)
		co_return;
	std::map<QString, std::pair<std::set<int>, std::set<int>>> lines;
	for (size_t i = 0; i < locations.size(); i++)
		if (locations[i].Line && !locations[i].Path.empty()) {
			auto& [all, disabled] = lines[QFileInfo(Q(locations[i].Path)).canonicalFilePath()];
			all.insert(static_cast<int>(locations[i].Line) - 1);
			if (!enabled[i])
				disabled.insert(static_cast<int>(locations[i].Line) - 1);
		}
	for (auto& [path, view] : m_Sources) {
		auto it = lines.find(QFileInfo(path).canonicalFilePath());
		view->SetDisabledBreakpointLines(it != lines.end() ? it->second.second : std::set<int>{});
		view->SetBreakpointLines(it != lines.end() ? it->second.first : std::set<int>{});
	}
}

void MainWindow::ToggleSourceBreakpoint(SourceView* view, int line) {
	ToggleSourceBreakpointTask(view->Path(), view->LocationFile(), line);
}

Task<void> MainWindow::ToggleSourceBreakpointTask(QString path, QString file, int line) {
	auto alive = m_Lifetime.Watch();
	std::vector<BreakpointInfo> breakpoints;
	co_await Debugger().EnumBreakpoints(breakpoints);
	std::vector<ULONG64> offsets;
	std::vector<ULONG> ids;
	for (auto& b : breakpoints)
		if (!b.IsData && b.Offset) {
			offsets.push_back(b.Offset);
			ids.push_back(b.Id);
		}
	std::vector<SourceLocation> locations;
	if (!offsets.empty())
		co_await Debugger().GetSourceLocations(offsets, locations);
	if (!*alive)
		co_return;
	auto canonical = QFileInfo(path).canonicalFilePath();
	bool removed = false;
	for (size_t i = 0; i < locations.size(); i++)
		if (static_cast<int>(locations[i].Line) == line + 1 && QFileInfo(Q(locations[i].Path)).canonicalFilePath() == canonical) {
			co_await Debugger().RemoveBreakpoint(ids[i]);
			removed = true;
		}
	if (removed || !*alive)
		co_return;
	auto hr = co_await Debugger().AddBreakpoint(Debugger().SourceLineLocation(W(file), line + 1).c_str());
	if (*alive && FAILED(hr))
		SetStatusMessage(tr("No breakpoint can be set at line %1: %2").arg(line + 1).arg(Q(Debugger().LastError())));
}

void MainWindow::RunToSourceLine(SourceView* view, int line) {
	if (Session().CanRun())
		Debugger().RunTo(Debugger().SourceLineLocation(W(view->LocationFile()), line + 1).c_str());
}

void MainWindow::SetNextStatementToLine(SourceView* view, int line) {
	[](MainWindow* self, QString file, int line) -> Task<void> {
		auto alive = self->m_Lifetime.Watch();
		ULONG64 address = 0;
		auto hr = co_await Debugger().ResolveLocation(Debugger().SourceLineLocation(W(file), line + 1).c_str(), address);
		if (!*alive)
			co_return;
		if (FAILED(hr))
			self->SetStatusMessage(tr("Line %1 has no code").arg(line + 1));
		else
			co_await self->SetNextStatementTask(address);
	}(this, view->LocationFile(), line);
}

void MainWindow::AddWatch(const QString& expression) {
	if (auto dock = findChild<QDockWidget*>("watch")) {
		dock->show();
		dock->raise();
	}
	m_Watch->AddWatch(expression);
}

void MainWindow::GoToDisassembly(const QString& expression) {
	if (auto dock = findChild<QDockWidget*>("disassembly")) {
		dock->show();
		dock->raise();
	}
	m_Disassembly->GoToAddress(expression);
}

//
// Source documents
//

SourceView* MainWindow::OpenSource(const QString& path, bool activate) {
	auto it = m_Sources.find(path);
	SourceView* view;
	if (it != m_Sources.end())
		view = it->second;
	else {
		view = new SourceView(path, m_Documents);
		if (!view->Load()) {
			delete view;
			SetStatusMessage(tr("Cannot open %1").arg(path));
			return nullptr;
		}
		m_Sources[path] = view;
		auto index = m_Documents->addTab(view, QFileInfo(path).fileName());
		m_Documents->setTabToolTip(index, path);
		UpdateBreakpointMarkers();
	}
	// (without taking the focus from where the user is, unless asked)
	m_Documents->setCurrentWidget(view);
	if (activate)
		view->setFocus();
	return view;
}

Task<void> MainWindow::UpdateSourceLocation() {
	auto alive = m_Lifetime.Watch();
	auto generation = ++m_LocationGeneration;
	if (!Session().HasTarget() || !Session().IsBroken())
		co_return;
	ULONG64 ip = 0, scopeIp = 0;
	ULONG frame = 0;
	co_await Debugger().GetInstructionOffset(ip);
	co_await Debugger().GetScope(scopeIp, frame);
	std::vector<SourceLocation> locations;
	co_await Debugger().GetSourceLocations({ frame ? scopeIp - 1 : ip }, locations);
	if (!*alive || generation != m_LocationGeneration)
		co_return;
	for (auto& [_, view] : m_Sources)
		view->SetCurrentLine(-1, false);
	if (locations.empty() || !locations[0].Line) {
		SetStatusMessage(tr("No source information at 0x%1").arg(frame ? scopeIp : ip, 0, 16));
		co_return;
	}
	auto& location = locations[0];
	if (location.Path.empty()) {
		SetStatusMessage(tr("Source file not found: %1 (Options > Symbol and Source Paths)").arg(Q(location.File)));
		co_return;
	}
	auto view = OpenSource(Q(location.Path), false);
	if (!view)
		co_return;
	view->SetSymbolFile(Q(location.File));
	view->SetCurrentLine(static_cast<int>(location.Line) - 1, frame > 0);
	view->ShowLine(static_cast<int>(location.Line) - 1);
	SetStatusMessage(QStringLiteral("%1:%2").arg(Q(location.Path)).arg(location.Line));
}

void MainWindow::ShowAddressSource(ULONG64 address) {
	ShowAddressSourceTask(address);
}

Task<void> MainWindow::ShowAddressSourceTask(ULONG64 address) {
	auto alive = m_Lifetime.Watch();
	std::vector<SourceLocation> locations;
	co_await Debugger().GetSourceLocations({ address }, locations);
	if (!*alive)
		co_return;
	if (!locations.empty() && !locations[0].Path.empty()) {
		if (auto view = OpenSource(Q(locations[0].Path), true)) {
			view->SetSymbolFile(Q(locations[0].File));
			view->ShowLine(static_cast<int>(locations[0].Line) - 1);
		}
	}
	else
		GoToDisassembly(QStringLiteral("0x%1").arg(address, 0, 16));
}

Task<void> MainWindow::ShowNextStatement() {
	auto alive = m_Lifetime.Watch();
	if (!Session().HasTarget() || !Session().IsBroken())
		co_return;
	bool fromDisassembly = ActivePane() == m_Disassembly;
	if (Session().CurrentFrame() != 0)
		co_await Debugger().SetScopeFrame(0);
	ULONG64 ip = 0;
	co_await Debugger().GetInstructionOffset(ip);
	if (!*alive)
		co_return;
	if (fromDisassembly)
		GoToDisassembly({});
	else
		co_await ShowAddressSourceTask(ip);
}

//
// The active pane
//

PaneBase* MainWindow::ActivePane() const {
	for (auto w = QApplication::focusWidget(); w; w = w->parentWidget())
		if (auto pane = dynamic_cast<PaneBase*>(w))
			return pane;
	return nullptr;
}

SourceView* MainWindow::ActiveSource() const {
	return dynamic_cast<SourceView*>(m_Documents->currentWidget());
}

void MainWindow::Find(bool next, bool backward) {
	// the text view with the focus (a Scintilla, or a plain text one), else the current document, else the Command view's
	QsciScintilla* editor = nullptr;
	QPlainTextEdit* text = nullptr;
	for (auto w = QApplication::focusWidget(); w && !editor && !text; w = w->parentWidget()) {
		editor = qobject_cast<QsciScintilla*>(w);
		text = qobject_cast<QPlainTextEdit*>(w);
	}
	if (!editor && !text)
		editor = ActiveSource();
	if (!editor && !text)
		editor = m_Command->findChild<QsciScintilla*>();
	if (!editor && !text)
		return;
	if (!next || m_FindText.isEmpty()) {
		auto selected = editor ? editor->selectedText() : text->textCursor().selectedText();
		bool ok;
		auto what = QInputDialog::getText(this, tr("Find"), tr("Find what:"), QLineEdit::Normal,
			selected.isEmpty() || selected.contains('\n') ? m_FindText : selected, &ok);
		if (!ok || what.isEmpty())
			return;
		m_FindText = what;
	}
	bool found, wrapped = false;
	if (editor) {
		// from the end of the selection (backward: its start), so that the text found last is passed
		int line, index, lineTo, indexTo;
		editor->getSelection(&line, &index, &lineTo, &indexTo);
		if (line < 0)
			editor->getCursorPosition(&line, &index);
		else if (!backward) {
			line = lineTo;
			index = indexTo;
		}
		found = editor->findFirst(m_FindText, false, false, false, false, !backward, line, index);
		if (!found) {
			// round past the end (or the start)
			int last = std::max(editor->lines() - 1, 0);
			found = backward ? editor->findFirst(m_FindText, false, false, false, false, false, last, editor->text(last).length())
				: editor->findFirst(m_FindText, false, false, false, false, true, 0, 0);
			wrapped = true;
		}
	}
	else {
		QTextDocument::FindFlags flags;
		if (backward)
			flags |= QTextDocument::FindBackward;
		found = text->find(m_FindText, flags);
		if (!found) {
			auto cursor = text->textCursor();
			cursor.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
			text->setTextCursor(cursor);
			found = text->find(m_FindText, flags);
			wrapped = true;
		}
	}
	if (!found)
		SetStatusMessage(tr("'%1' not found").arg(m_FindText));
	else if (wrapped)
		SetStatusMessage(tr("Passed the %1 of the text").arg(backward ? tr("start") : tr("end")));
}
