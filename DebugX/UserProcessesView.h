#pragma once

#include "ListPane.h"
#include "resource.h"

// The processes being debugged in user mode: the one that was run or attached to, and the ones it starts if child
// processes are debugged (Options > Debug Child Processes). The current one -- the one the commands and the other panes
// are about -- has the arrow; a double click makes another one current. Right-click: also detach from it, or end it.
class CUserProcessesView : public CListPane<CUserProcessesView> {
public:
	void InitList();
	CString GetColumnText(HWND hWnd, int row, int column) const;
	int GetRowImage(HWND hWnd, int row, int col) const;
	void DoSort(const SortInfo* si);
	bool OnDoubleClickList(HWND, int row, int col, POINT const& pt);
	bool OnRightClickList(HWND, int row, int col, POINT const& pt);

	// ISessionObserver
	void OnProcessCreated(const DebugEngine::CreateProcessData&) override {
		RequestRefresh();
	}
	void OnProcessExited(const DebugEngine::ExitThreadData&) override {
		RequestRefresh();
	}
	void OnThreadCreated(const DebugEngine::ThreadCreateData&) override {
		MarkDirty();
	}
	void OnThreadExited(const DebugEngine::ExitThreadData&) override {
		MarkDirty();
	}
	void OnContextChanged() override {
		RequestRefresh();
	}
	void OnTargetEnded() override;

	// CPaneBase
	bool CanRefresh() const;
	DebugEngine::Task<void> Refresh();

private:
	enum class Action { Switch, Detach, Terminate };
	DebugEngine::Task<void> Apply(ULONG processId, Action action);

	std::vector<DebugEngine::ProcessInfo> m_Items;
};
