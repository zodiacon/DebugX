#pragma once

#include "VirtualListView.h"
#include "resource.h"
#include "Debugger.h"
#include "Lifetime.h"
#include "PaneBase.h"

class CUserThreadsView :
	public CFrameWindowImpl<CUserThreadsView, CWindow, CControlWinTraits>,
	public CVirtualListView<CUserThreadsView>,
	public CPaneBase<CUserThreadsView> {
public:
	using BaseFrame = CFrameWindowImpl<CUserThreadsView, CWindow, CControlWinTraits>;

	CString GetColumnText(HWND hWnd, int row, int column) const;
	int GetRowImage(HWND hWnd, int row, int col) const;
	void DoSort(const SortInfo* si);

	BEGIN_MSG_MAP(CUserThreadsView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		CHAIN_MSG_MAP(CPaneBase<CUserThreadsView>)
		CHAIN_MSG_MAP(BaseFrame)
		CHAIN_MSG_MAP(CVirtualListView<CUserThreadsView>)
	END_MSG_MAP();

	// IPane
	HWND Create(HWND hParent) override;
	std::wstring SaveState() const override;
	void LoadState(PCWSTR state) override;
	void OnFinalMessage(HWND) override;

	// ISessionObserver: the threads that come and go while the target runs
	void OnThreadCreated(const DebugEngine::ThreadCreateData& data) override;
	void OnThreadExited(const DebugEngine::ExitThreadData& data) override;
	void OnTargetEnded() override;

	// CPaneBase
	bool CanRefresh() const;
	DebugEngine::Task<void> Refresh();

private:
	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	void UpdateList();

private:
	CListViewCtrl m_List;
	std::vector<DebugEngine::ThreadInfo> m_Items;
	Lifetime m_Lifetime;
};
