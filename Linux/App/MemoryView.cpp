#include "MemoryView.h"
#include "GdbDebugger.h"
#include "MainWindow.h"
#include "CodeEditor.h"
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextBlock>
#include <QVBoxLayout>

using namespace DebugEngine;

MemoryView::MemoryView(QWidget* parent) : PaneBase(parent) {
	auto layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(2);
	auto bar = new QHBoxLayout;
	m_Address = new QLineEdit(QStringLiteral("$sp"), this);
	m_Address->setPlaceholderText(tr("Address expression ($sp, &variable, 0x...)"));
	auto go = new QPushButton(tr("Go"), this);
	bar->addWidget(m_Address);
	bar->addWidget(go);
	layout->addLayout(bar);
	m_Text = new QPlainTextEdit(this);
	m_Text->setReadOnly(true);
	m_Text->setLineWrapMode(QPlainTextEdit::NoWrap);
	m_Text->setFont(CodeFont());
	m_Text->setContextMenuPolicy(Qt::NoContextMenu);
	layout->addWidget(m_Text);
	connect(m_Address, &QLineEdit::returnPressed, this, [this] { MarkDirty(); });
	connect(go, &QPushButton::clicked, this, [this] { MarkDirty(); });
}

QString MemoryView::Expression() const {
	return m_Address->text();
}

void MemoryView::SetExpression(const QString& expression) {
	if (!expression.isEmpty())
		m_Address->setText(expression);
	MarkDirty();
}

bool MemoryView::GoTo() {
	m_Address->setFocus();
	m_Address->selectAll();
	return true;
}

void MemoryView::Clear() {
	m_Text->clear();
	m_Bytes.clear();
	m_Shown = 0;
}

Task<void> MemoryView::Refresh() {
	auto alive = m_Lifetime.Watch();
	auto expression = m_Address->text().trimmed();
	if (expression.isEmpty())
		co_return;
	ULONG64 address = 0;
	auto hr = co_await Debugger().Evaluate(W(expression).c_str(), address);
	if (!*alive)
		co_return;
	if (FAILED(hr)) {
		m_Text->setPlainText(tr("Cannot evaluate '%1': %2").arg(expression, Q(Debugger().LastError())));
		co_return;
	}
	auto start = address & ~0xFull;
	std::vector<BYTE> bytes;
	co_await Debugger().ReadMemory(start, bytes, Size);
	if (!*alive)
		co_return;

	QString text;
	for (ULONG line = 0; line < Size / 16; line++) {
		auto lineAddress = start + line * 16;
		text += HexAddress(lineAddress) + "  ";
		QString chars;
		for (ULONG i = 0; i < 16; i++) {
			auto index = line * 16 + i;
			if (index < bytes.size()) {
				text += QStringLiteral("%1 ").arg(bytes[index], 2, 16, QLatin1Char('0'));
				chars += bytes[index] >= 0x20 && bytes[index] < 0x7f ? QChar(bytes[index]) : QChar('.');
			}
			else {
				text += "?? ";
				chars += ' ';
			}
			if (i == 7)
				text += ' ';
		}
		text += ' ' + chars + '\n';
	}
	text.chop(1);
	m_Text->setPlainText(text);

	// what changed since the last refresh of the same address, in red
	QList<QTextEdit::ExtraSelection> changed;
	if (start == m_Shown) {
		for (size_t i = 0; i < bytes.size() && i < m_Bytes.size(); i++) {
			if (bytes[i] == m_Bytes[i])
				continue;
			auto block = m_Text->document()->findBlockByNumber(static_cast<int>(i / 16));
			int column = 18 + static_cast<int>(i % 16) * 3 + (i % 16 >= 8 ? 1 : 0);
			QTextEdit::ExtraSelection s;
			s.cursor = QTextCursor(block);
			s.cursor.setPosition(block.position() + column);
			s.cursor.setPosition(block.position() + column + 2, QTextCursor::KeepAnchor);
			s.format.setForeground(QColor(230, 40, 40));
			changed << s;
		}
	}
	m_Text->setExtraSelections(changed);
	m_Shown = start;
	m_Bytes = std::move(bytes);
}

void MemoryView::contextMenuEvent(QContextMenuEvent* event) {
	QMenu menu;
	bool live = Session().CanRun() && (Session().Target() & TargetType::Live) == TargetType::Live;
	menu.addAction(tr("&Write Bytes..."), this, [this] { WriteBytes(); })->setEnabled(live);
	menu.addAction(tr("&Go to Address..."), QKeySequence(Qt::CTRL | Qt::Key_G), this, [this] { GoTo(); });
	menu.addSeparator();
	menu.addAction(tr("&Copy"), QKeySequence::Copy, m_Text, &QPlainTextEdit::copy)->setEnabled(m_Text->textCursor().hasSelection());
	menu.exec(event->globalPos());
}

Task<void> MemoryView::WriteBytes() {
	auto alive = m_Lifetime.Watch();
	bool ok;
	auto text = QInputDialog::getText(this, tr("Write Bytes"), tr("Address expression, then the bytes in hexadecimal (\"&g_buffer 01 02 ff\"):"),
		QLineEdit::Normal, HexAddress(m_Shown) + " ", &ok);
	if (!ok || !*alive)
		co_return;
	auto parts = text.split(' ', Qt::SkipEmptyParts);
	if (parts.size() < 2)
		co_return;
	std::vector<BYTE> data;
	for (int i = 1; i < parts.size(); i++) {
		bool valid;
		auto b = parts[i].toUInt(&valid, 16);
		if (!valid || b > 0xff) {
			QMessageBox::warning(this, tr("Write Bytes"), tr("'%1' is not a byte").arg(parts[i]));
			co_return;
		}
		data.push_back(static_cast<BYTE>(b));
	}
	auto expr = parts[0];
	if (!expr.startsWith("0x") && expr.size() == 16)
		expr = "0x" + expr;
	ULONG64 address = 0;
	auto hr = co_await Debugger().Evaluate(W(expr).c_str(), address);
	if (!*alive)
		co_return;
	if (SUCCEEDED(hr))
		hr = co_await Debugger().WriteMemory(address, data);
	if (*alive && FAILED(hr))
		QMessageBox::warning(this, tr("Write Bytes"), tr("The bytes could not be written: %1").arg(Q(Debugger().LastError())));
}
