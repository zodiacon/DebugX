#include "ListPanes.h"
#include "GdbDebugger.h"
#include "MainWindow.h"
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QScrollBar>
#include <QUrl>
#include <QVBoxLayout>

using namespace DebugEngine;

//
// ListPane
//

ListPane::ListPane(const QStringList& headers, bool filter, QWidget* parent) : PaneBase(parent) {
	auto layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(2);
	if (filter) {
		m_Filter = new QLineEdit(this);
		m_Filter->setPlaceholderText(tr("Filter (Ctrl+F)"));
		m_Filter->setClearButtonEnabled(true);
		m_Filter->installEventFilter(this);
		connect(m_Filter, &QLineEdit::textChanged, this, [this] { ApplyFilter(); });
		layout->addWidget(m_Filter);
	}
	m_List = new QTreeWidget(this);
	m_List->setHeaderLabels(headers);
	m_List->setRootIsDecorated(false);
	m_List->setUniformRowHeights(true);
	m_List->setAlternatingRowColors(true);
	m_List->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_List->setContextMenuPolicy(Qt::CustomContextMenu);
	m_List->installEventFilter(this);
	m_List->setFont(CodeFont());
	if (!headers.isEmpty() && headers[0].isEmpty())
		m_List->header()->resizeSection(0, 24);		// (a marker column: the current frame, thread)
	layout->addWidget(m_List);
}

QByteArray ListPane::SaveState() const {
	return m_List->header()->saveState();
}

void ListPane::RestoreState(const QByteArray& state) {
	if (!state.isEmpty())
		m_List->header()->restoreState(state);
}

QTreeWidgetItem* ListPane::AddRow(const QStringList& texts) {
	return new QTreeWidgetItem(m_List, texts);
}

void ListPane::BeginUpdate() {
	auto current = m_List->currentItem();
	m_SavedRow = current ? m_List->indexOfTopLevelItem(current) : -1;
	m_SavedScroll = m_List->verticalScrollBar()->value();
	m_List->setUpdatesEnabled(false);
	m_List->clear();
}

void ListPane::EndUpdate() {
	if (m_SavedRow >= 0 && m_SavedRow < m_List->topLevelItemCount())
		m_List->setCurrentItem(m_List->topLevelItem(m_SavedRow));
	ApplyFilter();
	m_List->setUpdatesEnabled(true);
	m_List->verticalScrollBar()->setValue(m_SavedScroll);
}

void ListPane::ApplyFilter() {
	auto text = m_Filter ? m_Filter->text() : QString();
	for (int i = 0; i < m_List->topLevelItemCount(); i++) {
		auto item = m_List->topLevelItem(i);
		bool show = text.isEmpty();
		for (int c = 0; !show && c < item->columnCount(); c++)
			show = item->text(c).contains(text, Qt::CaseInsensitive);
		item->setHidden(!show);
	}
}

void ListPane::CopySelection(bool all) {
	QList<QTreeWidgetItem*> rows;
	for (int i = 0; i < m_List->topLevelItemCount(); i++) {
		auto item = m_List->topLevelItem(i);
		if (!item->isHidden() && (all || item->isSelected()))
			rows.push_back(item);
	}
	if (rows.isEmpty())
		return;
	QString text;
	auto header = m_List->headerItem();
	if (rows.size() > 1) {
		QStringList cells;
		for (int c = 0; c < header->columnCount(); c++)
			cells << header->text(c);
		text += cells.join('\t') + '\n';
	}
	for (auto row : rows) {
		QStringList cells;
		for (int c = 0; c < header->columnCount(); c++)
			cells << row->text(c);
		text += cells.join('\t') + '\n';
	}
	QApplication::clipboard()->setText(text);
}

void ListPane::AddCopyItems(QMenu& menu) {
	if (!menu.isEmpty())
		menu.addSeparator();
	menu.addAction(tr("&Copy"), QKeySequence::Copy, this, [this] { CopySelection(); });
	menu.addAction(tr("Copy &All"), this, [this] { CopySelection(true); });
}

bool ListPane::eventFilter(QObject* watched, QEvent* event) {
	if (event->type() == QEvent::KeyPress) {
		auto key = static_cast<QKeyEvent*>(event);
		if (watched == m_List && key->matches(QKeySequence::Copy)) {
			CopySelection();
			return true;
		}
		if (m_Filter && key->matches(QKeySequence::Find)) {
			m_Filter->setFocus();
			m_Filter->selectAll();
			return true;
		}
		if (watched == m_Filter && key->key() == Qt::Key_Escape) {
			m_Filter->clear();
			m_List->setFocus();
			return true;
		}
	}
	return PaneBase::eventFilter(watched, event);
}

//
// Call Stack
//

CallStackView::CallStackView(QWidget* parent) : ListPane({ "", "#", tr("Function"), tr("Source"), tr("Instruction"), tr("Return"), tr("Stack") }, false, parent) {
	m_List->setSelectionMode(QAbstractItemView::SingleSelection);
	connect(m_List, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
		SwitchTo(item->data(1, Qt::UserRole).toUInt());
	});
	connect(m_List, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
		auto item = m_List->itemAt(pos);
		QMenu menu;
		if (item) {
			auto index = item->data(1, Qt::UserRole).toUInt();
			bool can = Session().CanRun();
			menu.addAction(tr("&Switch to Frame"), this, [this, index] { SwitchTo(index); })->setEnabled(can);
			menu.addAction(tr("&Run to Frame"), this, [index, this] {
				if (index > 0 && index < m_Frames.size())
					MainWindow::Get().RunToAddress(m_Frames[index].InstructionOffset);
			})->setEnabled(can && index > 0);
			menu.addAction(tr("Go to &Disassembly"), this, [this, index] {
				if (index < m_Frames.size())
					MainWindow::Get().GoToDisassembly(QStringLiteral("0x%1").arg(m_Frames[index].InstructionOffset, 0, 16));
			});
		}
		AddCopyItems(menu);
		menu.exec(m_List->viewport()->mapToGlobal(pos));
	});
}

void CallStackView::Clear() {
	m_Frames.clear();
	m_List->clear();
}

Task<void> CallStackView::SwitchTo(ULONG frame) {
	if (!Session().CanRun() && !(Session().HasTarget() && Session().IsBroken()))
		co_return;
	co_await Debugger().SetScopeFrame(frame);
}

Task<void> CallStackView::Refresh() {
	auto alive = m_Lifetime.Watch();
	std::vector<StackFrameInfo> frames;
	co_await Debugger().GetStackTrace(frames);
	if (!*alive)
		co_return;
	std::vector<ULONG64> offsets;
	for (auto& f : frames)
		offsets.push_back(f.Index == 0 ? f.InstructionOffset : f.InstructionOffset - 1);
	std::vector<SourceLocation> locations;
	co_await Debugger().GetSourceLocations(offsets, locations);
	if (!*alive)
		co_return;
	m_Frames = std::move(frames);
	BeginUpdate();
	auto current = Session().CurrentFrame();
	for (size_t i = 0; i < m_Frames.size(); i++) {
		auto& f = m_Frames[i];
		QString source;
		if (i < locations.size() && locations[i].Line)
			source = QStringLiteral("%1:%2").arg(QFileInfo(Q(locations[i].File)).fileName()).arg(locations[i].Line);
		auto item = AddRow({ f.Index == current ? QStringLiteral("▶") : QString(), QString::number(f.Index),
			f.Symbol.empty() ? QStringLiteral("0x%1").arg(f.InstructionOffset, 0, 16) : Q(f.Symbol), source,
			HexAddress(f.InstructionOffset), f.ReturnOffset ? HexAddress(f.ReturnOffset) : QString(), HexAddress(f.StackOffset) });
		item->setData(1, Qt::UserRole, f.Index);
		if (f.Index == current) {
			auto font = item->font(2);
			font.setBold(true);
			for (int c = 0; c < item->columnCount(); c++)
				item->setFont(c, font);
		}
	}
	EndUpdate();
}

//
// Registers
//

RegistersView::RegistersView(QWidget* parent) : ListPane({ tr("Register"), tr("Value") }, false, parent) {
	m_List->setSelectionMode(QAbstractItemView::SingleSelection);
	connect(m_List, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
		Edit(item->text(0), item->text(1));
	});
	connect(m_List, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
		QMenu menu;
		if (auto item = m_List->itemAt(pos)) {
			auto name = item->text(0), value = item->text(1);
			menu.addAction(tr("&Edit Value..."), this, [this, name, value] { Edit(name, value); })
				->setEnabled(Session().CanRun() && Session().CurrentFrame() == 0);
		}
		AddCopyItems(menu);
		menu.exec(m_List->viewport()->mapToGlobal(pos));
	});
}

void RegistersView::Clear() {
	m_List->clear();
	m_Previous.clear();
}

Task<void> RegistersView::Edit(QString name, QString value) {
	auto alive = m_Lifetime.Watch();
	if (!Session().CanRun() || Session().CurrentFrame() != 0)
		co_return;
	bool ok;
	auto text = QInputDialog::getText(this, tr("Edit Register"), name, QLineEdit::Normal, value, &ok);
	if (!ok || !*alive)
		co_return;
	auto hr = co_await Debugger().SetRegister(W(name).c_str(), W(text).c_str());
	if (*alive && FAILED(hr))
		QMessageBox::warning(this, tr("Edit Register"), tr("The value could not be set: %1").arg(Q(Debugger().LastError())));
}

Task<void> RegistersView::Refresh() {
	auto alive = m_Lifetime.Watch();
	std::vector<RegisterInfo> registers;
	co_await Debugger().GetRegisters(registers);
	if (!*alive)
		co_return;
	// (in red: what changed since the last stop, in the same thread and frame)
	bool compare = Session().CurrentThreadId() == m_PreviousThread && Session().CurrentFrame() == m_PreviousFrame;
	std::map<QString, QString> values;
	BeginUpdate();
	for (auto& r : registers) {
		auto name = Q(r.Name), value = Q(r.Value);
		auto item = AddRow({ name, value });
		auto old = m_Previous.find(name);
		if (compare && old != m_Previous.end() && old->second != value)
			item->setForeground(1, QColor(220, 40, 40));
		values[name] = value;
	}
	EndUpdate();
	m_Previous = std::move(values);
	m_PreviousThread = Session().CurrentThreadId();
	m_PreviousFrame = Session().CurrentFrame();
}

//
// Modules
//

ModulesView::ModulesView(QWidget* parent) : ListPane({ tr("Name"), tr("Base"), tr("Size"), tr("Symbols"), tr("Path") }, true, parent) {
	m_List->setSortingEnabled(true);
	m_List->sortByColumn(1, Qt::AscendingOrder);
	connect(m_List, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
		QMenu menu;
		if (auto item = m_List->itemAt(pos)) {
			auto path = item->text(4);
			auto name = item->text(0);
			menu.addAction(tr("&Load Symbols"), this, [name] { Debugger().ReloadSymbols(W(name).c_str()); })->setEnabled(Session().CanRun());
			menu.addAction(tr("&Open Folder"), this, [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath())); });
			menu.addAction(tr("Copy &Path"), this, [path] { QApplication::clipboard()->setText(path); });
		}
		AddCopyItems(menu);
		menu.exec(m_List->viewport()->mapToGlobal(pos));
	});
}

void ModulesView::Clear() {
	m_List->clear();
}

Task<void> ModulesView::Refresh() {
	auto alive = m_Lifetime.Watch();
	std::vector<ModuleInfo> modules;
	co_await Debugger().EnumModules(modules);
	if (!*alive)
		co_return;
	m_List->setSortingEnabled(false);
	BeginUpdate();
	for (auto& m : modules)
		AddRow({ Q(m.Name), HexAddress(m.BaseAddress), QStringLiteral("0x%1").arg(m.Size, 0, 16),
			m.Symbols == 3 ? tr("Loaded") : m.Symbols == 5 ? tr("Deferred") : tr("None"), Q(m.ImageName) });
	EndUpdate();
	m_List->setSortingEnabled(true);
}

//
// Threads
//

ThreadsView::ThreadsView(QWidget* parent) : ListPane({ "", tr("Id"), tr("TID"), tr("Name"), tr("Location"), tr("Process") }, true, parent) {
	connect(m_List, &QTreeWidget::itemDoubleClicked, this, [](QTreeWidgetItem* item) {
		if (Session().CanRun())
			Debugger().SetCurrentThread(item->text(1).toUInt());
	});
	connect(m_List, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
		QMenu menu;
		if (auto item = m_List->itemAt(pos)) {
			auto id = item->text(1).toUInt();
			menu.addAction(tr("&Switch to Thread"), this, [id] { Debugger().SetCurrentThread(id); })->setEnabled(Session().CanRun());
		}
		AddCopyItems(menu);
		menu.exec(m_List->viewport()->mapToGlobal(pos));
	});
}

void ThreadsView::Clear() {
	m_List->clear();
}

Task<void> ThreadsView::Refresh() {
	auto alive = m_Lifetime.Watch();
	std::vector<ProcessInfo> processes;
	std::vector<ThreadInfo> threads;
	co_await Debugger().EnumProcesses(processes, threads);
	if (!*alive)
		co_return;
	BeginUpdate();
	for (auto& t : threads) {
		auto item = AddRow({ t.Current ? QStringLiteral("▶") : QString(), QString::number(t.Id), QString::number(t.ThreadId), Q(t.Name),
			t.Location.empty() ? HexAddress(t.InstructionOffset) : Q(t.Location), QString::number(t.Pid) });
		if (t.Current) {
			auto font = item->font(1);
			font.setBold(true);
			for (int c = 0; c < item->columnCount(); c++)
				item->setFont(c, font);
		}
	}
	EndUpdate();
}

//
// Breakpoints (GDB keeps them between runs: they are shown without a target too)
//

BreakpointsView::BreakpointsView(QWidget* parent) : ListPane({ tr("Id"), tr("Location"), tr("Condition"), tr("Hits"), tr("Address"), tr("Kind") }, false, parent) {
	connect(m_List, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
		if (!m_Building && column == 0)
			SetEnabled(item->data(0, Qt::UserRole).toUInt(), item->checkState(0) == Qt::Checked);
	});
	connect(m_List, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int column) {
		auto id = item->data(0, Qt::UserRole).toUInt();
		if (column == 2)
			EditCondition(id);
		else
			for (auto& b : m_Breakpoints)
				if (b.Id == id && b.Offset)
					MainWindow::Get().ShowAddressSource(b.Offset);
	});
	connect(m_List, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
		QMenu menu;
		auto& main = MainWindow::Get();
		menu.addAction(tr("&Add Breakpoint..."), &main, [&main] { main.AddBreakpointDialog(); });
		if (auto item = m_List->itemAt(pos)) {
			auto id = item->data(0, Qt::UserRole).toUInt();
			menu.addAction(tr("Edit &Condition..."), this, [this, id] { EditCondition(id); });
			menu.addAction(tr("&Delete"), QKeySequence::Delete, this, [this] { DeleteSelected(); });
		}
		menu.addSeparator();
		menu.addAction(tr("&Enable All"), &main, [&main] { main.SetAllBreakpointsEnabled(true); });
		menu.addAction(tr("D&isable All"), &main, [&main] { main.SetAllBreakpointsEnabled(false); });
		menu.addAction(tr("Delete A&ll"), &main, [&main] { main.DeleteAllBreakpoints(); });
		AddCopyItems(menu);
		menu.exec(m_List->viewport()->mapToGlobal(pos));
	});
	auto del = new QAction(this);
	del->setShortcut(QKeySequence::Delete);
	del->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	connect(del, &QAction::triggered, this, [this] { DeleteSelected(); });
	addAction(del);
}

bool BreakpointsView::CanRefresh() const {
	return !Session().IsRunning();
}

Task<void> BreakpointsView::SetEnabled(ULONG id, bool enabled) {
	co_await Debugger().SetBreakpointEnabled(id, enabled);
}

Task<void> BreakpointsView::EditCondition(ULONG id) {
	auto alive = m_Lifetime.Watch();
	auto it = std::find_if(m_Breakpoints.begin(), m_Breakpoints.end(), [&](auto& b) { return b.Id == id; });
	if (it == m_Breakpoints.end() || Session().IsRunning())
		co_return;
	BreakpointSettings settings = *it;
	bool ok;
	auto text = QInputDialog::getText(this, tr("Breakpoint Condition"), tr("Break only when this C++ expression is not zero (empty: always):"),
		QLineEdit::Normal, Q(settings.Condition), &ok);
	if (!ok || !*alive)
		co_return;
	settings.Condition = W(text);
	auto hr = co_await Debugger().UpdateBreakpoint(id, settings);
	if (*alive && FAILED(hr))
		QMessageBox::warning(this, tr("Breakpoint Condition"), Q(Debugger().LastError()));
}

Task<void> BreakpointsView::DeleteSelected() {
	std::vector<ULONG> ids;
	for (auto item : m_List->selectedItems())
		ids.push_back(item->data(0, Qt::UserRole).toUInt());
	for (auto id : ids)
		co_await Debugger().RemoveBreakpoint(id);
}

Task<void> BreakpointsView::Refresh() {
	auto alive = m_Lifetime.Watch();
	std::vector<BreakpointInfo> breakpoints;
	co_await Debugger().EnumBreakpoints(breakpoints);
	if (!*alive)
		co_return;
	m_Breakpoints = std::move(breakpoints);
	m_Building = true;
	BeginUpdate();
	for (auto& b : m_Breakpoints) {
		QString kind = b.IsData ? (b.DataAccess & BreakOnRead ? tr("Read/write %1").arg(b.DataSize) : tr("Write %1").arg(b.DataSize)) :
			b.OneShot ? tr("One-shot") : tr("Code");
		auto item = AddRow({ QString::number(b.Id), Q(b.Location.empty() ? b.Expression : b.Location), Q(b.Condition), QString::number(b.Hits),
			b.Deferred ? tr("(pending)") : HexAddress(b.Offset), kind });
		item->setCheckState(0, b.Enabled ? Qt::Checked : Qt::Unchecked);
		item->setData(0, Qt::UserRole, b.Id);
	}
	EndUpdate();
	m_Building = false;
}
