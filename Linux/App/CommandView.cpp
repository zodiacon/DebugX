#include "CommandView.h"
#include "GdbDebugger.h"
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QMenu>
#include <QVBoxLayout>
#include <Qsci/qsciscintilla.h>

using namespace DebugEngine;

//
// The output: a read-only Scintilla without a lexer, styled as text is appended (CCommandView's m_Text)
//
class CommandOutput final : public QsciScintilla {
public:
	enum Style { Normal = 0, Error, Warning, Debuggee, Echo };
	static constexpr int MaxLines = 20000;

	explicit CommandOutput(QWidget* parent) : QsciScintilla(parent) {
		setUtf8(true);
		setReadOnly(true);
		setWrapMode(WrapWord);
		setMarginWidth(0, 0);
		setMarginWidth(1, 0);
		setCaretWidth(0);
		SendScintilla(SCI_SETUNDOCOLLECTION, 0ul);
		ApplyStyles();
	}

	void Append(const QString& text, int style) {
		if (text.isEmpty())
			return;
		bool atEnd = AtEnd();
		auto utf8 = text.toUtf8();
		auto start = SendScintilla(SCI_GETLENGTH);
		// (Scintilla's append honors read-only)
		SendScintilla(SCI_SETREADONLY, 0ul);
		SendScintilla(SCI_APPENDTEXT, static_cast<unsigned long>(utf8.size()), utf8.constData());
		SendScintilla(SCI_STARTSTYLING, static_cast<unsigned long>(start));
		SendScintilla(SCI_SETSTYLING, static_cast<unsigned long>(utf8.size()), static_cast<long>(style));
		// bounded: the oldest lines go
		if (int extra = lines() - MaxLines; extra > 0) {
			auto end = SendScintilla(SCI_POSITIONFROMLINE, static_cast<unsigned long>(extra));
			SendScintilla(SCI_DELETERANGE, 0ul, static_cast<long>(end));
		}
		SendScintilla(SCI_SETREADONLY, 1ul);
		if (atEnd) {
			auto length = SendScintilla(SCI_GETLENGTH);
			SendScintilla(SCI_SCROLLRANGE, static_cast<unsigned long>(length), static_cast<long>(length));
		}
	}

	void ApplyStyles() {
		auto font = CodeFont();
		auto palette = QPalette();
		auto fore = palette.color(QPalette::Text), back = palette.color(QPalette::Base);
		bool dark = back.lightness() < 128;
		SendScintilla(SCI_STYLERESETDEFAULT);
		setFont(font);		// (STYLE_DEFAULT, then copied to every style)
		setColor(fore);
		setPaper(back);
		SendScintilla(SCI_STYLECLEARALL);
		auto styleFore = [this](int style, const QColor& color) {
			SendScintilla(SCI_STYLESETFORE, static_cast<unsigned long>(style), color);
		};
		styleFore(Error, dark ? QColor(255, 110, 110) : QColor(200, 0, 0));
		styleFore(Warning, dark ? QColor(230, 180, 60) : QColor(170, 110, 0));
		styleFore(Debuggee, dark ? QColor(110, 170, 240) : QColor(0, 90, 190));
		styleFore(Echo, palette.color(QPalette::Disabled, QPalette::Text));
		setSelectionBackgroundColor(palette.color(QPalette::Highlight));
		setSelectionForegroundColor(palette.color(QPalette::HighlightedText));
	}

	void Clear() {
		clear();
	}

protected:
	void changeEvent(QEvent* event) override {
		QsciScintilla::changeEvent(event);
		if (event->type() == QEvent::PaletteChange)
			ApplyStyles();
	}

	void contextMenuEvent(QContextMenuEvent* event) override {
		QMenu menu;
		menu.addAction(tr("&Copy"), QKeySequence::Copy, this, &QsciScintilla::copy)->setEnabled(hasSelectedText());
		menu.addAction(tr("Select &All"), QKeySequence::SelectAll, this, [this] { selectAll(); });
		menu.addSeparator();
		menu.addAction(tr("C&lear Output"), this, [this] { Clear(); });
		menu.exec(event->globalPos());
	}

private:
	// whether the end shows (then what is appended stays in view; else the view stays where the user put it)
	bool AtEnd() const {
		auto last = static_cast<unsigned long>(std::max(lines() - 1, 0));
		auto lastVisible = SendScintilla(SCI_VISIBLEFROMDOCLINE, last) + SendScintilla(SCI_WRAPCOUNT, last);
		return SendScintilla(SCI_GETFIRSTVISIBLELINE) + SendScintilla(SCI_LINESONSCREEN) >= lastVisible;
	}
};

CommandView::CommandView(QWidget* parent) : QWidget(parent) {
	auto layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(2);
	m_Output = new CommandOutput(this);
	layout->addWidget(m_Output);
	auto row = new QHBoxLayout;
	auto prompt = new QLabel(QStringLiteral("(gdb)"), this);
	m_Input = new QLineEdit(this);
	m_Input->installEventFilter(this);
	row->addWidget(prompt);
	row->addWidget(m_Input);
	layout->addLayout(row);
	ApplyFont();
	connect(m_Input, &QLineEdit::returnPressed, this, [this] {
		auto command = m_Input->text();
		m_Input->clear();
		Execute(command);
	});
	Session().AddObserver(this);
	OnSessionChanged();
}

CommandView::~CommandView() {
	Session().RemoveObserver(this);
}

void CommandView::ApplyFont() {
	m_Output->ApplyStyles();
	m_Input->setFont(CodeFont());
}

void CommandView::FocusInput() {
	m_Input->setFocus();
}

void CommandView::Append(const QString& text, int style) {
	m_Output->Append(text, style);
}

void CommandView::OnOutput(const OutputData& data) {
	int style = CommandOutput::Normal;
	if ((data.Mask & OutputDataMask::Error) == OutputDataMask::Error)
		style = CommandOutput::Error;
	else if ((data.Mask & OutputDataMask::Warning) == OutputDataMask::Warning)
		style = CommandOutput::Warning;
	else if ((data.Mask & OutputDataMask::Debuggee) == OutputDataMask::Debuggee)
		style = CommandOutput::Debuggee;
	else if ((data.Mask & OutputDataMask::Prompt) == OutputDataMask::Prompt)
		return;		// (the prompt is the label)
	Append(Q(data.Text), style);
}

void CommandView::OnSessionChanged() {
	// GDB takes commands without a target too (file, help, set ...); not while the target runs
	m_Input->setEnabled(!Session().IsRunning());
}

Task<void> CommandView::Execute(QString command) {
	auto alive = m_Lifetime.Watch();
	auto trimmed = command.trimmed();
	if (trimmed.isEmpty() && !m_History.isEmpty())
		trimmed = m_History.back();		// (Enter repeats the last command, as in GDB)
	if (trimmed.isEmpty())
		co_return;
	if (m_History.isEmpty() || m_History.back() != trimmed) {
		m_History << trimmed;
		while (m_History.size() > 100)
			m_History.removeFirst();
	}
	m_HistoryIndex = static_cast<int>(m_History.size());
	if (trimmed == "cls" || trimmed == ".cls") {
		m_Output->Clear();
		co_return;
	}
	Append(QStringLiteral("(gdb) %1\n").arg(trimmed), CommandOutput::Echo);
	co_await Debugger().ExecuteCommand(W(trimmed).c_str());
	if (*alive && isVisible())
		m_Input->setFocus();
}

bool CommandView::eventFilter(QObject* watched, QEvent* event) {
	if (watched == m_Input && event->type() == QEvent::KeyPress) {
		auto key = static_cast<QKeyEvent*>(event)->key();
		if (key == Qt::Key_Up && m_HistoryIndex > 0) {
			m_Input->setText(m_History[--m_HistoryIndex]);
			return true;
		}
		if (key == Qt::Key_Down) {
			if (m_HistoryIndex + 1 < m_History.size())
				m_Input->setText(m_History[++m_HistoryIndex]);
			else {
				m_HistoryIndex = static_cast<int>(m_History.size());
				m_Input->clear();
			}
			return true;
		}
	}
	return QWidget::eventFilter(watched, event);
}
