#pragma once

#include "ListPane.h"
#include "resource.h"

// The threads of the processes being debugged: their names (SetThreadDescription), where they are, their suspend
// counts, which are frozen. A double click makes a thread the current one; the right-click menu also freezes and
// thaws it (the debugger does not let a frozen thread run when the target runs), and suspends and resumes it.
class CUserThreadsView : public CListPane<CUserThreadsView> {
public:
	void InitList();
	CString GetColumnText(HWND hWnd, int row, int column) const;
	int GetRowImage(HWND hWnd, int row, int col) const;
	void DoSort(const SortInfo* si);
	bool HasFilter() const {
		return true;
	}
	// a double click makes the thread the current one
	bool OnDoubleClickList(HWND, int row, int col, POINT const& pt);
	bool OnRightClickList(HWND, int row, int col, POINT const& pt);

	// ISessionObserver: the threads that come and go while the target runs, and which one is current
	void OnThreadCreated(const DebugEngine::ThreadCreateData& data) override;
	void OnThreadExited(const DebugEngine::ExitThreadData& data) override;
	void OnContextChanged() override;
	void OnTargetEnded() override;

	// CPaneBase
	bool CanRefresh() const;
	DebugEngine::Task<void> Refresh();

private:
	DebugEngine::Task<void> SwitchTo(ULONG threadId);
	enum class Action { Freeze, Thaw, Suspend, Resume };
	DebugEngine::Task<void> Apply(ULONG threadId, Action action);

	std::vector<DebugEngine::ThreadInfo> m_Items;
};
