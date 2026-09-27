#pragma once

#include "VirtualListView.h"
#include "PaneBase.h"
#include "Lifetime.h"

//
// A pane that is a list of what the target has (threads, modules, registers, ...): a virtual list view that fills
// itself (CPaneBase), sorts (CVirtualListView) and keeps the width of its columns. What is left to the pane:
//
//   class CMyPane : public CListPane<CMyPane> {
//       BEGIN_MSG_MAP(CMyPane)
//           ... its own messages ...
//           CHAIN_MSG_MAP(CListPane<CMyPane>)
//       END_MSG_MAP()
//       void InitList();                                        // the columns (and the images)
//       CString GetColumnText(HWND, int row, int column) const;   // what CVirtualListView asks for
//       DebugEngine::Task<void> Refresh();                      // ... ending in RefreshDone(); then UpdateList(count)
//   };
//
template<typename T>
class CListPane :
	public CFrameWindowImpl<T, CWindow, CControlWinTraits>,
	public CVirtualListView<T>,
	public CPaneBase<T> {
public:
	using BaseFrame = CFrameWindowImpl<T, CWindow, CControlWinTraits>;

	BEGIN_MSG_MAP(CListPane)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		CHAIN_MSG_MAP(CPaneBase<T>)
		CHAIN_MSG_MAP(BaseFrame)
		CHAIN_MSG_MAP(CVirtualListView<T>)
	END_MSG_MAP()

	// IPane
	HWND Create(HWND hParent) override {
		return BaseFrame::Create(hParent, CWindow::rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
	}

	std::wstring SaveState() const override {
		return ColumnState::SaveColumnWidths(m_List);
	}

	void LoadState(PCWSTR state) override {
		ColumnState::LoadColumnWidths(m_List, state);
	}

	void OnFinalMessage(HWND) override {
		delete static_cast<T*>(this);
	}

	// for the pane to hide
	void InitList() {}
	DWORD ExtendedStyle() const {
		return LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP;
	}

protected:
	// the list shows 'count' rows (in the order the pane keeps them in: sorted first, as the user last asked)
	void UpdateList(size_t count) {
		static_cast<T*>(this)->DoSort(this->GetSortInfo(m_List));
		m_List.SetItemCountEx(static_cast<int>(count), LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
		m_List.RedrawItems(m_List.GetTopIndex(), m_List.GetTopIndex() + m_List.GetCountPerPage());
	}

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
		auto pT = static_cast<T*>(this);
		this->m_hWndClient = m_List.Create(*this, CWindow::rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS
			| LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS);
		m_List.SetExtendedListViewStyle(pT->ExtendedStyle());
		pT->InitList();
		this->InitPane();
		return 0;
	}

	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
		this->ExitPane();
		handled = FALSE;
		return 0;
	}

protected:
	CListViewCtrl m_List;
	Lifetime m_Lifetime;
};
