#pragma once

#include <atomic>
#include <memory>
#include "Debugger.h"
#include "resource.h"
#include "SortedFilteredVector.h"
#include "VirtualListView.h"
#include "Lifetime.h"
#include "PaneBase.h"

class CKernelProcessesView :
	public CFrameWindowImpl<CKernelProcessesView, CWindow, CControlWinTraits>,
	public CVirtualListView<CKernelProcessesView>,
	public CPaneBase<CKernelProcessesView> {
public:
	using BaseFrame = CFrameWindowImpl<CKernelProcessesView, CWindow, CControlWinTraits>;

	BEGIN_MSG_MAP(CKernelProcessesView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		COMMAND_ID_HANDLER(ID_VIEW_REFRESH, OnViewRefresh)
		NOTIFY_CODE_HANDLER(LVN_GETDISPINFO, OnListGetDispInfo)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		CHAIN_MSG_MAP(CPaneBase<CKernelProcessesView>)
		CHAIN_MSG_MAP(BaseFrame)
		CHAIN_MSG_MAP(CVirtualListView<CKernelProcessesView>)
	END_MSG_MAP();

	// IPane
	HWND Create(HWND hParent) override;
	std::wstring SaveState() const override;
	void LoadState(PCWSTR state) override;
	void OnFinalMessage(HWND) override;

	// ISessionObserver: the processes of a kernel are looked at when the target is new, or when asked for (Refresh
	// in the View menu), not every time it stops
	void OnBroke() override {}
	void OnTargetEnded() override;

	// CPaneBase
	bool CanRefresh() const;
	DebugEngine::Task<void> Refresh();

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

	void DoSort(const SortInfo* si);

private:
	// The processes are collected on the debug thread, which can outlive this window (the enumeration is slow), so
	// what it works on is shared with the coroutine that waits for it rather than owned by the window.
	struct Enumeration {
		std::vector<KernelProcess> Processes;
		std::atomic<size_t> Count{ 0 };
	};

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnTimer(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnViewRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnListGetDispInfo(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

private:
	CListViewCtrl m_List;
	SortedFilteredVector<KernelProcess> m_Items;
	std::shared_ptr<Enumeration> m_Enumeration;
	Lifetime m_Lifetime;
};
