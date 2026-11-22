#pragma once

#include <atomic>
#include <memory>
#include "ListPane.h"
#include "resource.h"
#include "SortedFilteredVector.h"

class CKernelProcessesView : public CListPane<CKernelProcessesView> {
public:
	BEGIN_MSG_MAP(CKernelProcessesView)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		COMMAND_ID_HANDLER(ID_VIEW_REFRESH, OnViewRefresh)
		NOTIFY_CODE_HANDLER(LVN_GETDISPINFO, OnListGetDispInfo)
		CHAIN_MSG_MAP(CListPane<CKernelProcessesView>)
	END_MSG_MAP()

	struct KernelProcess {
		DWORD Pid;
		ULONG64 Address;
		CString ImageName;
		ULONG ThreadCount;
		LARGE_INTEGER CreateTime;
		LARGE_INTEGER ExitTime;
		ULONG SessionId;
		ULONG64 DirBase;
		ULONG64 Peb;
		ULONG64 KernelTime, UserTime;
	};

	void InitList();
	void DoSort(const SortInfo* si);
	// the filter box: by name, PID and address -- of the processes it has (all of them, m_Items keeps them), without
	// walking the kernel's list again
	bool HasFilter() const {
		return true;
	}
	void FilterChanged();
	// Switch to Process (.process /p /r: the debugger looks at its user-mode memory and modules; the Kernel Threads pane
	// lists its threads), also invasively on a kernel over a connection; Details (dt nt!_EPROCESS)
	bool OnDoubleClickList(HWND, int row, int col, POINT const& pt);
	bool OnRightClickList(HWND, int row, int col, POINT const& pt);

	// ISessionObserver: the processes of a kernel are looked at when the target is new, or when asked for (Refresh
	// in the View menu), not every time it stops; which one the debugger looks at is marked
	void OnBroke() override {}
	void OnContextChanged() override {
		// (a walk that found no symbols is tried again: they may have come since, Reload Symbols too ends here)
		if (m_NoSymbols)
			RequestRefresh();
		UpdateCurrent();
	}
	void OnTargetEnded() override;

	// CPaneBase
	bool CanRefresh() const;
	DebugEngine::Task<void> Refresh();

private:
	// The processes are collected on the debug thread, which can outlive this window (the enumeration is slow), so
	// what it works on is shared with the coroutine that waits for it rather than owned by the window.
	struct Enumeration {
		std::vector<KernelProcess> Processes;
		std::atomic<size_t> Count{ 0 };
		// KernelProcessEnumerator's IsValid, IsComplete
		bool Valid{ true }, Complete{ true };
	};

	LRESULT OnTimer(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnViewRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnListGetDispInfo(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

	// the rows: the processes, then a row for m_Message (why there are none, or not all)
	void UpdateRows();

	// the process the debugger looks at, marked in the list
	DebugEngine::Task<void> UpdateCurrent();
	DebugEngine::Task<void> SwitchTo(ULONG64 process, bool invasive);

private:
	SortedFilteredVector<KernelProcess> m_Items;
	ULONG64 m_Current{ 0 };
	CString m_Message;
	bool m_NoSymbols{ false };
	std::shared_ptr<Enumeration> m_Enumeration;
};
