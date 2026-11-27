#include "OutputView.h"
#include "Common.h"
#include <QContextMenuEvent>
#include <QMenu>
#include <QScrollBar>

OutputView::OutputView(QWidget* parent) : QPlainTextEdit(parent) {
	setReadOnly(true);
	setMaximumBlockCount(5000);
	setLineWrapMode(QPlainTextEdit::NoWrap);
	setFont(CodeFont());
	for (auto& line : Session().Log())
		appendPlainText(Q(line));
	Session().AddObserver(this);
}

OutputView::~OutputView() {
	Session().RemoveObserver(this);
}

void OutputView::OnLogLine(const std::wstring& line) {
	auto bar = verticalScrollBar();
	bool atEnd = bar->value() >= bar->maximum() - 2;
	appendPlainText(Q(line));
	if (atEnd)
		bar->setValue(bar->maximum());
}

void OutputView::contextMenuEvent(QContextMenuEvent* event) {
	QMenu menu;
	menu.addAction(tr("&Copy"), QKeySequence::Copy, this, &QPlainTextEdit::copy)->setEnabled(textCursor().hasSelection());
	menu.addAction(tr("Select &All"), QKeySequence::SelectAll, this, &QPlainTextEdit::selectAll);
	menu.addAction(tr("C&lear"), this, &QPlainTextEdit::clear);
	menu.exec(event->globalPos());
}
