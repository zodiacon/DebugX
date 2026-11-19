#pragma once

#include "ListPane.h"
#include "resource.h"

// The threads of the kernel process the debugger looks at (Processes > Switch to Process): ETHREAD, thread ID, state,
// where it started. Double-click (or the right-click menu): the debugger looks at that thread (.thread), so the call
// stack and the registers are its; Details is dt nt!_ETHREAD in the Command view (not !thread: the engines DebugX uses have no extension DLLs).
class CKernelThreadsView : public CListPane<CKernelThreadsView> {
public:
	void InitList();
	CString GetColumnText(HWND hWnd, int row, int column) const;
	int GetRowImage(HWND, int row, int col) const;
	void DoSort(const SortInfo* si);
	bool OnDoubleClickList(HWND, int row, int col, POINT const& pt);
	bool OnRightClickList(HWND, int row, int col, POINT const& pt);

	// ISessionObserver: another process or thread looked at
	void OnContextChanged() override {
		RequestRefresh();
	}
	void OnTargetEnded() override;

	// CPaneBase
	bool CanRefresh() const;
	DebugEngine::Task<void> Refresh();

	struct KernelThread {
		ULONG64 Address;
		ULONG Tid;
		UCHAR State;
		ULONG64 StartAddress;
		std::wstring StartSymbol;
		LARGE_INTEGER CreateTime;
		bool Current;
	};

private:
	DebugEngine::Task<void> SwitchTo(ULONG64 thread);

	std::vector<KernelThread> m_Items;
};
