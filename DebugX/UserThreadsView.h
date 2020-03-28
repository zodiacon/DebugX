#pragma once

#include "VirtualListView.h"
#include "Interfaces.h"

class CUserThreadsView :
	public CFrameWindowImpl<CUserThreadsView, CWindow, CControlWinTraits>,
	public CVirtualListView<CUserThreadsView>,
	public IDebugXView {
public:
	using BaseFrame = CFrameWindowImpl<CUserThreadsView, CWindow, CControlWinTraits>;

	struct ThreadInfo {
		DWORD Id;
		DWORD ProcessId;
		PVOID Teb;
	};

	BEGIN_MSG_MAP(CUserThreadsView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
//		COMMAND_ID_HANDLER(ID_VIEW_REFRESH, OnViewRefresh)
		NOTIFY_CODE_HANDLER(LVN_GETDISPINFO, OnListGetDispInfo)
		CHAIN_MSG_MAP(BaseFrame)
		CHAIN_MSG_MAP(CVirtualListView<CUserThreadsView>)
	END_MSG_MAP();

	// Inherited via IDebugXView
	virtual HICON GetIcon() const override;
	virtual CString GetHeader() const override;
	virtual HWND Create(HWND hParent) override;

private:
	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnViewRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnListGetDispInfo(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

private:
	CListViewCtrl m_List;
	std::vector<ThreadInfo> m_Items;
};

