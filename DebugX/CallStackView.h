#pragma once

#include "ListPane.h"
#include "resource.h"

// The call stack of the current thread. (In the order of the frames: it is not sorted.)
class CCallStackView : public CListPane<CCallStackView> {
public:
	void InitList();
	CString GetColumnText(HWND hWnd, int row, int column) const;
	int GetRowImage(HWND, int row, int col) const {
		return row == 0 ? 1 : 0;
	}
	bool IsSortable(HWND, int) const {
		return false;
	}

	// ISessionObserver: another thread's stack
	void OnContextChanged() override {
		RequestRefresh();
	}
	void OnTargetEnded() override;

	// CPaneBase
	DebugEngine::Task<void> Refresh();

private:
	std::vector<DebugEngine::StackFrameInfo> m_Items;
};
