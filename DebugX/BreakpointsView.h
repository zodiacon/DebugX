#pragma once

#include "ListPane.h"
#include "resource.h"

// The breakpoints of the session, whoever set them (here, or with bp in the Command window). The check box (or Space)
// is whether one is enabled; Delete removes the selected one, Insert (and the menu) adds one, a double click or Enter
// edits it (condition, command, hit count, thread, ...).
class CBreakpointsView : public CListPane<CBreakpointsView> {
public:
	BEGIN_MSG_MAP(CBreakpointsView)
		NOTIFY_CODE_HANDLER(LVN_KEYDOWN, OnKeyDown)
		CHAIN_MSG_MAP(CListPane<CBreakpointsView>)
	END_MSG_MAP()

	void InitList();
	DWORD ExtendedStyle() const {
		return CListPane<CBreakpointsView>::ExtendedStyle() | LVS_EX_CHECKBOXES;
	}
	CString GetColumnText(HWND hWnd, int row, int column) const;
	int GetRowImage(HWND, int row, int col) const {
		return -1;
	}
	ListViewRowCheck IsRowChecked(HWND, int row) const;
	void DoSort(const SortInfo* si);

	// CVirtualListView: a click on the check box (a virtual list does not say when one is toggled), the menu, a double
	// click (which edits)
	void OnListViewClick(HWND, int row, int col, POINT const& pt);
	bool OnRightClickList(HWND, int row, int col, POINT const& pt);
	bool OnDoubleClickList(HWND, int row, int col, POINT const& pt);

	// ISessionObserver
	void OnBreakpointsChanged() override {
		RequestRefresh();
	}
	void OnTargetEnded() override;

	// CPaneBase
	DebugEngine::Task<void> Refresh();

private:
	LRESULT OnKeyDown(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

	void AddBreakpoint();
	void RemoveSelected();
	void ToggleSelected();
	void EditSelected();
	DebugEngine::Task<void> Update(ULONG id, DebugEngine::BreakpointSettings settings);
	DebugEngine::Task<void> Remove(ULONG id);
	DebugEngine::Task<void> SetEnabled(ULONG id, bool enabled);

	std::vector<DebugEngine::BreakpointInfo> m_Items;
};
