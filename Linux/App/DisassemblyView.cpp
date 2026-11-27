#include "DisassemblyView.h"
#include "CodeEditor.h"
#include "GdbDebugger.h"
#include "MainWindow.h"
#include <QContextMenuEvent>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QTextStream>
#include <QVBoxLayout>

using namespace DebugEngine;

namespace {
	class DisassemblyText final : public CodeEditor {
	public:
		DisassemblyText(DisassemblyView* view) : CodeEditor(Language::Asm, view), m_View(view) {
			SetShowLineNumbers(false);
		}

	protected:
		void contextMenuEvent(QContextMenuEvent* event) override {
			if (event->reason() == QContextMenuEvent::Mouse)
				setCursorPosition(LineAt(event->pos()), 0);
			bool canRun = Session().CanRun();
			QMenu menu;
			menu.addAction(tr("Toggle &Breakpoint"), QKeySequence(Qt::Key_F9), this, [this] { m_View->ToggleBreakpoint(); })->setEnabled(Session().HasTarget());
			menu.addAction(tr("Run to &Cursor"), QKeySequence(Qt::CTRL | Qt::Key_F10), this, [this] { m_View->RunToCursor(); })->setEnabled(canRun);
			menu.addAction(tr("Set &Next Statement"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F10), this, [this] { m_View->SetNextStatement(); })->setEnabled(canRun);
			menu.addAction(tr("&Go to Address..."), QKeySequence(Qt::CTRL | Qt::Key_G), this, [this] { m_View->GoTo(); });
			menu.addSeparator();
			auto source = menu.addAction(tr("Show &Source Lines"), this, [this](bool on) { m_View->SetShowSource(on); });
			source->setCheckable(true);
			source->setChecked(m_View->ShowSource());
			menu.addSeparator();
			menu.addAction(tr("&Copy"), QKeySequence::Copy, this, &QsciScintilla::copy)->setEnabled(hasSelectedText());
			menu.exec(event->globalPos());
		}

	private:
		DisassemblyView* m_View;
	};
}

DisassemblyView::DisassemblyView(QWidget* parent) : PaneBase(parent) {
	auto layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(2);
	auto bar = new QHBoxLayout;
	m_Address = new QLineEdit(this);
	m_Address->setPlaceholderText(tr("Address, symbol or expression (empty: the current instruction)"));
	m_Address->setClearButtonEnabled(true);
	auto go = new QPushButton(tr("Go"), this);
	bar->addWidget(m_Address);
	bar->addWidget(go);
	layout->addLayout(bar);
	m_Text = new DisassemblyText(this);
	layout->addWidget(m_Text);
	connect(m_Address, &QLineEdit::returnPressed, this, [this] { GoToAddress(m_Address->text()); });
	connect(go, &QPushButton::clicked, this, [this] { GoToAddress(m_Address->text()); });
	connect(m_Text, &CodeEditor::GutterClicked, this, [this](int line) {
		if (line >= 0 && line < static_cast<int>(m_LineAddresses.size()) && m_LineAddresses[line])
			ToggleAt(m_LineAddresses[line]);
	});
}

void DisassemblyView::SetShowSource(bool show) {
	m_ShowSource = show;
	MarkDirty();
}

void DisassemblyView::Clear() {
	m_Text->clear();
	m_LineAddresses.clear();
	m_From = 0;
	m_Files.clear();
}

void DisassemblyView::OnBroke() {
	m_From = 0;
	m_Address->clear();
	m_Files.clear();		// (the files may have been edited between runs)
	MarkDirty();
}

void DisassemblyView::OnContextChanged() {
	m_From = 0;
	m_Address->clear();
	MarkDirty();
}

void DisassemblyView::OnScopeChanged() {
	m_From = 0;
	m_Address->clear();
	MarkDirty();
}

void DisassemblyView::GoToAddress(const QString& expression) {
	m_Address->setText(expression);
	if (expression.trimmed().isEmpty()) {
		m_From = 0;
		MarkDirty();
		return;
	}
	Evaluate(expression.trimmed());
}

Task<void> DisassemblyView::Evaluate(QString expression) {
	auto alive = m_Lifetime.Watch();
	ULONG64 address = 0;
	auto hr = co_await Debugger().Evaluate(W(expression).c_str(), address);
	if (!*alive)
		co_return;
	if (FAILED(hr) || address == 0) {
		MainWindow::Get().SetStatusMessage(tr("Cannot evaluate '%1': %2").arg(expression, Q(Debugger().LastError())));
		co_return;
	}
	m_From = address;
	MarkDirty();
}

QString DisassemblyView::SourceLine(const QString& path, ULONG line) {
	auto it = m_Files.find(path);
	if (it == m_Files.end()) {
		QStringList lines;
		QFile file(path);
		if (file.open(QIODevice::ReadOnly)) {
			QTextStream in(&file);
			while (!in.atEnd())
				lines << in.readLine();
		}
		it = m_Files.emplace(path, std::move(lines)).first;
	}
	return line >= 1 && line <= static_cast<ULONG>(it->second.size()) ? it->second[line - 1].trimmed() : QString();
}

Task<void> DisassemblyView::Refresh() {
	auto alive = m_Lifetime.Watch();
	ULONG64 ip = 0, scopeIp = 0;
	ULONG frame = 0;
	co_await Debugger().GetInstructionOffset(ip);
	if (!*alive)
		co_return;
	co_await Debugger().GetScope(scopeIp, frame);
	if (!*alive)
		co_return;
	auto center = m_From ? m_From : (frame ? scopeIp : ip);

	// a little before the address, if the instructions from there meet it (x86's lengths vary), else from it
	std::vector<DisassemblyLine> lines;
	if (!m_From) {
		co_await Debugger().Disassemble(center - 48, 120, lines);
		if (!*alive)
			co_return;
		if (std::none_of(lines.begin(), lines.end(), [&](auto& l) { return l.Address == center; }))
			lines.clear();
	}
	if (lines.empty())
		co_await Debugger().Disassemble(center, 100, lines);
	if (!*alive)
		co_return;

	std::vector<SourceLocation> locations;
	if (m_ShowSource) {
		std::vector<ULONG64> addresses;
		for (auto& l : lines)
			addresses.push_back(l.Address);
		co_await Debugger().GetSourceLocations(addresses, locations);
		if (!*alive)
			co_return;
	}
	std::vector<BreakpointInfo> breakpoints;
	co_await Debugger().EnumBreakpoints(breakpoints);
	if (!*alive)
		co_return;

	QString text;
	m_LineAddresses.clear();
	std::wstring lastFile;
	ULONG lastLine = 0;
	int centerLine = -1, ipLine = -1;
	std::set<int> breakpointLines, disabledLines;
	for (size_t i = 0; i < lines.size(); i++) {
		if (i < locations.size() && locations[i].Line && (locations[i].Line != lastLine || locations[i].File != lastFile)) {
			lastFile = locations[i].File;
			lastLine = locations[i].Line;
			auto path = Q(locations[i].Path);
			text += QStringLiteral("; %1:%2  %3\n").arg(QFileInfo(Q(lastFile)).fileName()).arg(lastLine).arg(path.isEmpty() ? QString() : SourceLine(path, lastLine));
			m_LineAddresses.push_back(0);
		}
		int lineNumber = static_cast<int>(m_LineAddresses.size());
		if (lines[i].Address == center)
			centerLine = lineNumber;
		if (lines[i].Address == ip)
			ipLine = lineNumber;
		for (auto& b : breakpoints)
			if (!b.IsData && b.Offset == lines[i].Address) {
				breakpointLines.insert(lineNumber);
				if (!b.Enabled)
					disabledLines.insert(lineNumber);
			}
		text += Q(lines[i].Text) + '\n';
		m_LineAddresses.push_back(lines[i].Address);
	}
	if (text.endsWith('\n'))
		text.chop(1);
	m_Text->setText(text);
	m_Text->SetBreakpointLines(breakpointLines);
	m_Text->SetDisabledBreakpointLines(disabledLines);
	if (frame && !m_From)
		m_Text->SetCurrentLine(centerLine, true);
	else
		m_Text->SetCurrentLine(ipLine, false);
	if (centerLine >= 0)
		m_Text->ShowLine(centerLine);
}

ULONG64 DisassemblyView::CaretAddress() const {
	auto line = m_Text->CaretLine();
	// (a source line: the first instruction after it)
	for (auto i = static_cast<size_t>(std::max(line, 0)); i < m_LineAddresses.size(); i++)
		if (m_LineAddresses[i])
			return m_LineAddresses[i];
	return 0;
}

Task<void> DisassemblyView::ToggleAt(ULONG64 address) {
	std::vector<BreakpointInfo> breakpoints;
	co_await Debugger().EnumBreakpoints(breakpoints);
	for (auto& b : breakpoints)
		if (!b.IsData && b.Offset == address) {
			co_await Debugger().RemoveBreakpoint(b.Id);
			co_return;
		}
	co_await Debugger().AddBreakpoint(Debugger().AddressLocation(address).c_str());
}

bool DisassemblyView::ToggleBreakpoint() {
	auto address = CaretAddress();
	if (!address || !Session().HasTarget())
		return false;
	ToggleAt(address);
	return true;
}

bool DisassemblyView::RunToCursor() {
	auto address = CaretAddress();
	if (!address || !Session().CanRun())
		return false;
	MainWindow::Get().RunToAddress(address);
	return true;
}

bool DisassemblyView::SetNextStatement() {
	auto address = CaretAddress();
	if (!address || !Session().CanRun())
		return false;
	MainWindow::Get().SetNextStatement(address);
	return true;
}

bool DisassemblyView::GoTo() {
	m_Address->setFocus();
	m_Address->selectAll();
	return true;
}
