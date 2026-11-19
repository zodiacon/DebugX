#pragma once

#include "SymbolPane.h"
#include "AppMessages.h"
#include "resource.h"

// Expressions the user wants to see the value of, evaluated in the scope frame whenever the target stops. The last
// row is for adding one: double click it, press Insert, or right-click. F2 edits an expression, Delete removes it.
// The expressions are kept between runs (with the pane's state).
class CWatchView : public CSymbolPane<CWatchView> {
public:
	BEGIN_MSG_MAP(CWatchView)
		NOTIFY_CODE_HANDLER(LVN_BEGINLABELEDIT, OnBeginLabelEdit)
		NOTIFY_CODE_HANDLER(LVN_ENDLABELEDIT, OnEndLabelEdit)
		MESSAGE_HANDLER(WM_ADDWATCH, OnAddWatch)
		CHAIN_MSG_MAP(CSymbolPane<CWatchView>)
	END_MSG_MAP()

	void InitList();
	CString GetColumnText(HWND hWnd, int row, int column) const;
	bool OnDoubleClickList(HWND hWnd, int row, int col, POINT const& pt);
	bool OnRightClickList(HWND, int row, int col, POINT const& pt);
	bool OnKey(UINT vk, int row);

	size_t RowCount() const {
		// and the one for a new expression
		return m_Entries.size() + 1;
	}

	DebugEngine::Task<HRESULT> Fetch(std::vector<DebugEngine::SymbolEntry>& entries) {
		return DebugManager::DebuggerX().EvaluateWatches(entries, m_Expressions, ExpandedPaths());
	}

	// ISessionObserver
	void OnTargetEnded() override;

	// IPane: the width of the columns, and the expressions
	std::wstring SaveState() const override;
	void LoadState(PCWSTR state) override;

private:
	LRESULT OnBeginLabelEdit(int, LPNMHDR hdr, BOOL&);
	LRESULT OnEndLabelEdit(int, LPNMHDR hdr, BOOL&);
	// Add to Watch, from another pane (through the frame): lParam is the expression
	LRESULT OnAddWatch(UINT, WPARAM, LPARAM lParam, BOOL&);

	// the expression a row is the value of (its tree's root), or -1: a member of one, or the row for a new one
	int ExpressionIndex(int row) const;
	bool IsNewRow(int row) const {
		return row == static_cast<int>(m_Entries.size());
	}
	void Edit(int row);
	void Remove(int row);
	// the expressions have changed: evaluated now if they can be, shown without values if not
	void ExpressionsChanged();
	void ShowUnevaluated();

	std::vector<std::wstring> m_Expressions;
	int m_EditRow{ -1 };
};
