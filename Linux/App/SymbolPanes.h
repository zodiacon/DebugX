#pragma once

#include <set>
#include <QTreeWidget>
#include "PaneBase.h"

//
// Locals and Watch (CSymbolPane on Windows): the engine's flattened tree of SymbolEntry rows, shown as a tree. What is
// expanded is kept by path (the names from the root) and asked for on each refresh, so no item is held across refreshes.
// Double-clicking a value that has an expression edits it (AssignValue).
//
class SymbolPane : public PaneBase {
public:
	explicit SymbolPane(QWidget* parent = nullptr);

protected:
	virtual DebugEngine::Task<HRESULT> Fetch(std::vector<DebugEngine::SymbolEntry>& entries, std::vector<std::wstring> expanded) = 0;
	DebugEngine::Task<void> Refresh() override;
	void Clear() override;
	void Show(const std::vector<DebugEngine::SymbolEntry>& entries);
	// what a refresh shows the entries with
	virtual void Display(const std::vector<DebugEngine::SymbolEntry>& entries) { Show(entries); }
	DebugEngine::Task<void> EditValue(QString expression, QString value);
	// an expression for the item: its own, else made from the names of the items it is in
	QString ExpressionOf(QTreeWidgetItem* item) const;
	void AddMenuItems(class QMenu& menu, QTreeWidgetItem* item);

	QTreeWidget* m_Tree;
	std::set<std::wstring> m_Expanded;
	bool m_Building{ false };
};

class LocalsView final : public SymbolPane {
public:
	explicit LocalsView(QWidget* parent = nullptr);

private:
	DebugEngine::Task<HRESULT> Fetch(std::vector<DebugEngine::SymbolEntry>& entries, std::vector<std::wstring> expanded) override;
};

class WatchView final : public SymbolPane {
public:
	explicit WatchView(QWidget* parent = nullptr);

	void AddWatch(const QString& expression);
	QStringList Expressions() const {
		return m_Expressions;
	}
	void SetExpressions(const QStringList& expressions);

private:
	DebugEngine::Task<HRESULT> Fetch(std::vector<DebugEngine::SymbolEntry>& entries, std::vector<std::wstring> expanded) override;
	bool CanRefresh() const override;
	void Clear() override;
	void ShowWithAddRow(const std::vector<DebugEngine::SymbolEntry>& entries);
	void Display(const std::vector<DebugEngine::SymbolEntry>& entries) override { ShowWithAddRow(entries); }
	void EditExpression(int index);
	void RemoveSelected();

	QStringList m_Expressions;
};
