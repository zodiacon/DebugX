#pragma once

#include "Debugger.h"
#include "Interfaces.h"
#include "resource.h"
#include "SortedFilteredVector.h"
#include "VirtualListView.h"

class CKernelProcessesView :
	public CFrameWindowImpl<CKernelProcessesView, CWindow, CControlWinTraits>,
	public CVirtualListView<CKernelProcessesView>,
	public IDebugXView {
public:
	using BaseFrame = CFrameWindowImpl<CKernelProcessesView, CWindow, CControlWinTraits>;

	BEGIN_MSG_MAP(CKernelProcessesView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		COMMAND_ID_HANDLER(ID_VIEW_REFRESH, OnViewRefresh)
		NOTIFY_CODE_HANDLER(LVN_GETDISPINFO, OnListGetDispInfo)
		CHAIN_MSG_MAP(BaseFrame)
		CHAIN_MSG_MAP(CVirtualListView<CKernelProcessesView>)
	END_MSG_MAP();

	// Inherited via IDebugXView
	virtual HICON GetIcon() const override;
	virtual CString GetHeader() const override;
	virtual DWORD GetWindowStyle() const override;

	struct KernelProcess {
		DWORD Pid;
		ULONG64 Address;
		CString ImageName;
		ULONG ThreadCount;
		LARGE_INTEGER CreateTime;
		ULONG SessionId;
		ULONG64 KernelTime, UserTime;
	};

	void Refresh();

private:
	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnTimer(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnViewRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnListGetDispInfo(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

private:
	CListViewCtrl m_List;
	SortedFilteredVector<KernelProcess> m_Items;
	bool m_Loading;
};

