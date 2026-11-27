#include "SymbolPanes.h"
#include "GdbDebugger.h"
#include "MainWindow.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QScrollBar>
#include <QVBoxLayout>

using namespace DebugEngine;

namespace {
	enum Roles { PathRole = Qt::UserRole, ExpressionRole, AddRowRole };
}

SymbolPane::SymbolPane(QWidget* parent) : PaneBase(parent) {
	auto layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	m_Tree = new QTreeWidget(this);
	m_Tree->setHeaderLabels({ tr("Name"), tr("Value"), tr("Type") });
	m_Tree->setUniformRowHeights(true);
	m_Tree->setAlternatingRowColors(true);
	m_Tree->setFont(CodeFont());
	m_Tree->setContextMenuPolicy(Qt::CustomContextMenu);
	m_Tree->header()->resizeSection(0, 180);
	m_Tree->header()->resizeSection(1, 260);
	layout->addWidget(m_Tree);

	connect(m_Tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) {
		if (m_Building)
			return;
		m_Expanded.insert(W(item->data(0, PathRole).toString()));
		MarkDirty();
	});
	connect(m_Tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* item) {
		if (m_Building)
			return;
		auto path = W(item->data(0, PathRole).toString());
		std::erase_if(m_Expanded, [&](auto& p) { return p == path || p.starts_with(path + L"\n"); });
	});
}

void SymbolPane::Clear() {
	m_Tree->clear();
}

Task<void> SymbolPane::Refresh() {
	auto alive = m_Lifetime.Watch();
	std::vector<SymbolEntry> entries;
	co_await Fetch(entries, std::vector<std::wstring>(m_Expanded.begin(), m_Expanded.end()));
	if (!*alive)
		co_return;
	Display(entries);
}

void SymbolPane::Show(const std::vector<SymbolEntry>& entries) {
	m_Building = true;
	auto scroll = m_Tree->verticalScrollBar()->value();
	auto current = m_Tree->currentItem() ? m_Tree->currentItem()->data(0, PathRole).toString() : QString();
	m_Tree->setUpdatesEnabled(false);
	m_Tree->clear();
	std::vector<QTreeWidgetItem*> parents;
	QTreeWidgetItem* select = nullptr;
	for (auto& e : entries) {
		parents.resize(std::min<size_t>(parents.size(), e.Depth));
		auto item = parents.empty() ? new QTreeWidgetItem(m_Tree) : new QTreeWidgetItem(parents.back());
		item->setText(0, Q(e.Name));
		item->setText(1, Q(e.Value));
		item->setText(2, Q(e.Type));
		item->setToolTip(1, Q(e.Value));
		item->setData(0, PathRole, Q(e.Path));
		item->setData(0, ExpressionRole, Q(e.Expression));
		if (e.Error)
			item->setForeground(1, QColor(220, 40, 40));
		if (e.Expandable)
			item->setChildIndicatorPolicy(e.Expanded ? QTreeWidgetItem::DontShowIndicatorWhenChildless : QTreeWidgetItem::ShowIndicator);
		if (Q(e.Path) == current)
			select = item;
		parents.push_back(item);
		if (e.Expanded)
			item->setExpanded(true);
	}
	// (expanded parents: their items came after them)
	for (QTreeWidgetItemIterator it(m_Tree); *it; ++it)
		if (m_Expanded.contains(W((*it)->data(0, PathRole).toString())))
			(*it)->setExpanded(true);
	if (select)
		m_Tree->setCurrentItem(select);
	m_Tree->setUpdatesEnabled(true);
	m_Tree->verticalScrollBar()->setValue(scroll);
	m_Building = false;
}

QString SymbolPane::ExpressionOf(QTreeWidgetItem* item) const {
	auto expression = item->data(0, ExpressionRole).toString();
	if (!expression.isEmpty())
		return expression;
	QStringList names;
	for (auto i = item; i; i = i->parent())
		names.prepend(i->text(0));
	return names.join('.');
}

Task<void> SymbolPane::EditValue(QString expression, QString value) {
	auto alive = m_Lifetime.Watch();
	if (expression.isEmpty() || !Session().CanRun())
		co_return;
	bool ok;
	auto text = QInputDialog::getText(this, tr("Edit Value"), expression, QLineEdit::Normal, value, &ok);
	if (!ok || !*alive)
		co_return;
	std::wstring error;
	auto hr = co_await Debugger().AssignValue(W(expression).c_str(), W(text).c_str(), error);
	if (*alive && FAILED(hr))
		QMessageBox::warning(this, tr("Edit Value"), Q(error));
}

void SymbolPane::AddMenuItems(QMenu& menu, QTreeWidgetItem* item) {
	if (!item || item->data(0, AddRowRole).toBool())
		return;
	auto expression = item->data(0, ExpressionRole).toString();
	auto value = item->text(1);
	menu.addAction(tr("&Edit Value..."), this, [this, expression, value] { EditValue(expression, value); })
		->setEnabled(!expression.isEmpty() && Session().CanRun());
	auto whole = ExpressionOf(item);
	menu.addAction(tr("Copy &Value"), this, [value] { QApplication::clipboard()->setText(value); });
	menu.addAction(tr("Copy E&xpression"), this, [whole] { QApplication::clipboard()->setText(whole); });
}

//
// Locals
//

LocalsView::LocalsView(QWidget* parent) : SymbolPane(parent) {
	connect(m_Tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int column) {
		if (column == 1)
			EditValue(item->data(0, ExpressionRole).toString(), item->text(1));
	});
	connect(m_Tree, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
		auto item = m_Tree->itemAt(pos);
		QMenu menu;
		if (item) {
			auto expression = ExpressionOf(item);
			menu.addAction(tr("&Add to Watch"), this, [expression] { MainWindow::Get().AddWatch(expression); });
		}
		AddMenuItems(menu, item);
		if (!menu.isEmpty())
			menu.exec(m_Tree->viewport()->mapToGlobal(pos));
	});
}

Task<HRESULT> LocalsView::Fetch(std::vector<SymbolEntry>& entries, std::vector<std::wstring> expanded) {
	co_return co_await Debugger().GetLocals(entries, std::move(expanded));
}

//
// Watch: the expressions are the user's (kept by the main window); the last row adds one
//

WatchView::WatchView(QWidget* parent) : SymbolPane(parent) {
	connect(m_Tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int column) {
		if (item->data(0, AddRowRole).toBool())
			EditExpression(-1);
		else if (column == 0 && !item->parent())
			EditExpression(m_Tree->indexOfTopLevelItem(item));
		else if (column == 1)
			EditValue(item->data(0, ExpressionRole).toString(), item->text(1));
	});
	connect(m_Tree, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
		auto item = m_Tree->itemAt(pos);
		QMenu menu;
		menu.addAction(tr("&Add Watch..."), this, [this] { EditExpression(-1); });
		if (item && !item->parent() && !item->data(0, AddRowRole).toBool()) {
			auto index = m_Tree->indexOfTopLevelItem(item);
			menu.addAction(tr("Edit E&xpression..."), this, [this, index] { EditExpression(index); });
			menu.addAction(tr("&Remove"), this, [this] { RemoveSelected(); });
		}
		menu.addAction(tr("Remove A&ll"), this, [this] { SetExpressions({}); });
		menu.addSeparator();
		AddMenuItems(menu, item);
		menu.exec(m_Tree->viewport()->mapToGlobal(pos));
	});
	auto del = new QAction(this);
	del->setShortcut(QKeySequence::Delete);
	del->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	connect(del, &QAction::triggered, this, [this] { RemoveSelected(); });
	addAction(del);
	ShowWithAddRow({});
}

bool WatchView::CanRefresh() const {
	return !Session().IsRunning();
}

void WatchView::Clear() {
	ShowWithAddRow({});
}

void WatchView::SetExpressions(const QStringList& expressions) {
	m_Expressions = expressions;
	std::vector<SymbolEntry> entries;
	for (auto& e : m_Expressions)
		entries.push_back({ W(e), W(e), {}, {}, {}, 0, false, false, false });
	ShowWithAddRow(entries);
	MarkDirty();
}

void WatchView::AddWatch(const QString& expression) {
	if (expression.trimmed().isEmpty())
		return;
	auto list = m_Expressions;
	list << expression.trimmed();
	SetExpressions(list);
}

void WatchView::EditExpression(int index) {
	bool ok;
	auto text = QInputDialog::getText(this, index < 0 ? tr("Add Watch") : tr("Edit Watch"), tr("Expression:"), QLineEdit::Normal,
		index >= 0 && index < m_Expressions.size() ? m_Expressions[index] : QString(), &ok).trimmed();
	if (!ok)
		return;
	auto list = m_Expressions;
	if (index >= 0 && index < list.size()) {
		if (text.isEmpty())
			list.removeAt(index);
		else
			list[index] = text;
	}
	else if (!text.isEmpty())
		list << text;
	SetExpressions(list);
}

void WatchView::RemoveSelected() {
	auto item = m_Tree->currentItem();
	while (item && item->parent())
		item = item->parent();
	if (!item || item->data(0, AddRowRole).toBool())
		return;
	auto list = m_Expressions;
	auto index = m_Tree->indexOfTopLevelItem(item);
	if (index >= 0 && index < list.size())
		list.removeAt(index);
	SetExpressions(list);
}

void WatchView::ShowWithAddRow(const std::vector<SymbolEntry>& entries) {
	Show(entries);
	auto add = new QTreeWidgetItem(m_Tree, { tr("(double-click to add)") });
	add->setForeground(0, palette().color(QPalette::Disabled, QPalette::Text));
	add->setData(0, AddRowRole, true);
}

Task<HRESULT> WatchView::Fetch(std::vector<SymbolEntry>& entries, std::vector<std::wstring> expanded) {
	std::vector<std::wstring> expressions;
	for (auto& e : m_Expressions)
		expressions.push_back(W(e));
	auto hr = co_await Debugger().EvaluateWatches(entries, std::move(expressions), std::move(expanded));
	co_return hr;
}
