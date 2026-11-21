#pragma once

#include "ListPane.h"
#include "resource.h"

// The handles of the current process of a live user-mode target (IDebugger::EnumHandles): type, name, access, the
// kernel object. The name of a file that is not on a disk (a pipe, the console) is not shown: asking for it can wait.
class CHandlesView : public CListPane<CHandlesView> {
public:
	void InitList();
	CString GetColumnText(HWND hWnd, int row, int column) const;
	int GetRowImage(HWND, int, int) const {
		return -1;
	}
	void DoSort(const SortInfo* si);
	bool OnRightClickList(HWND, int row, int col, POINT const& pt);

	// ISessionObserver: another process
	void OnContextChanged() override {
		RequestRefresh();
	}
	void OnTargetEnded() override;

	// CPaneBase
	bool CanRefresh() const;
	DebugEngine::Task<void> Refresh();

private:
	std::vector<DebugEngine::HandleInfo> m_Items;
};
