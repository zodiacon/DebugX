#pragma once

#include "VirtualListView.h"
#include "Interfaces.h"
#include "resource.h"
#include "Debugger.h"

class CUserThreadsView :
	public CFrameWindowImpl<CUserThreadsView, CWindow, CControlWinTraits>,
	public CVirtualListView<CUserThreadsView>,
	public IDebugXView {
public:
	using BaseFrame = CFrameWindowImpl<CUserThreadsView, CWindow, CControlWinTraits>;

	CString GetColumnText(HWND hWnd, int row, int column) const;

	BEGIN_MSG_MAP(CUserThreadsView)
		MESSAGE_HANDLER((UINT)DebugEngine::DebuggerNotificationType::StatusChanged, OnStatusChanged)
		MESSAGE_HANDLER((UINT)DebugEngine::DebuggerNotificationType::ThreadCreated, OnThreadCreated)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		//		COMMAND_ID_HANDLER(ID_VIEW_REFRESH, OnViewRefresh)
		CHAIN_MSG_MAP(BaseFrame)
		CHAIN_MSG_MAP(CVirtualListView<CUserThreadsView>)
	END_MSG_MAP();

	// Inherited via IDebugXView
	virtual HICON GetIcon() const override;
	virtual CString GetHeader() const override;
	virtual HWND Create(HWND hParent) override;

private:
	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnThreadCreated(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnStatusChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnViewRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	void Refresh();

private:
	CListViewCtrl m_List;
	std::vector<DebugEngine::ThreadInfo> m_Items;
};

