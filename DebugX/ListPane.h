#pragma once

#include "VirtualListView.h"
#include "PaneBase.h"
#include "Lifetime.h"
#include "AppFonts.h"
#include "ClipboardHelper.h"
#include "resource.h"

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
		MESSAGE_HANDLER(AppFonts::FontsChangedMessage, OnFontsChanged)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnEditCopy)
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

	// A pane without a right-click menu of its own gets Copy / Copy All. (One with its own puts them in it:
	// AppendCopyItems, then HandleCopyCommand with what TrackPopupMenu returned.)
	bool OnRightClickList(HWND, int, int, POINT const& pt) {
		CMenu menu;
		menu.CreatePopupMenu();
		AppendCopyItems(menu.m_hMenu);
		return HandleCopyCommand(menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, this->m_hWnd));
	}

protected:
	// the commands of AppendCopyItems (TPM_RETURNCMD: apart from the panes' own, which count from 1)
	enum { CmdCopyRows = 0x7F00, CmdCopyAllRows };

	void AppendCopyItems(CMenuHandle menu) const {
		if (menu.GetMenuItemCount() > 0)
			menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(m_List.GetSelectedCount() ? MF_STRING : MF_GRAYED, CmdCopyRows, L"&Copy\tCtrl+C");
		menu.AppendMenu(m_List.GetItemCount() ? MF_STRING : MF_GRAYED, CmdCopyAllRows, L"Copy A&ll");
	}

	// true if 'cmd' was one of AppendCopyItems'
	bool HandleCopyCommand(UINT cmd) {
		if (cmd != CmdCopyRows && cmd != CmdCopyAllRows)
			return false;
		CopyRows(cmd == CmdCopyAllRows);
		return true;
	}

	// The selected rows (or all of them), as the list shows them: the columns in their order on the screen, separated by
	// tabs -- with the column names first when there is more than one row, so that it pastes into a spreadsheet as a table.
	void CopyRows(bool all) {
		std::vector<int> rows;
		if (all) {
			for (int i = 0; i < m_List.GetItemCount(); i++)
				rows.push_back(i);
		}
		else {
			for (int i = m_List.GetNextItem(-1, LVNI_SELECTED); i >= 0; i = m_List.GetNextItem(i, LVNI_SELECTED))
				rows.push_back(i);
		}
		if (rows.empty())
			return;

		CHeaderCtrl header(m_List.GetHeader());
		const int columns = header.GetItemCount();
		std::vector<int> order(columns);
		m_List.GetColumnOrderArray(columns, order.data());

		CString text;
		auto line = [&](auto&& cell) {
			for (int i = 0; i < columns; i++) {
				if (i)
					text += L'\t';
				text += cell(order[i]);
			}
			text += L"\r\n";
			};
		if (rows.size() > 1) {
			line([&](int column) {
				WCHAR name[128]{};
				HDITEM item{ HDI_TEXT };
				item.pszText = name;
				item.cchTextMax = _countof(name);
				header.GetItem(column, &item);
				return CString(name);
				});
		}
		for (auto row : rows) {
			line([&](int column) {
				// (LVN_GETDISPINFO: what the pane shows; a tree's rows come indented)
				CString cell;
				m_List.GetItemText(row, column, cell);
				return cell;
				});
		}
		// (one row: no line break after it)
		if (rows.size() == 1)
			text.TrimRight(L"\r\n");
		ClipboardHelper::CopyText(this->m_hWnd, text);
	}

	// Ctrl+C (Edit > Copy, which the frame passes to the active pane): the selected rows -- unless an editor of the pane's
	// (a value being edited in place) has the focus, which copies its own selection
	LRESULT OnEditCopy(WORD, WORD, HWND, BOOL&) {
		auto focus = ::GetFocus();
		if (focus && focus != m_List.m_hWnd && this->IsChild(focus))
			::SendMessage(focus, WM_COPY, 0, 0);
		else
			CopyRows(false);
		return 0;
	}

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
		m_List.SetFont(AppFonts::UiFont());		// (before InitList: an editor it makes takes the list's font)
		pT->InitList();
		this->InitPane();
		return 0;
	}

	LRESULT OnFontsChanged(UINT, WPARAM wParam, LPARAM, BOOL& handled) {
		handled = FALSE;
		if (static_cast<AppFonts::Kind>(wParam) != AppFonts::Kind::Ui)
			return 0;
		m_List.SetFont(AppFonts::UiFont());
		// and what the pane put on the list (an in-place editor); the header has taken it from the list
		HWND header = m_List.GetHeader();
		for (CWindow child = m_List.GetWindow(GW_CHILD); child; child = child.GetWindow(GW_HWNDNEXT))
			if (child != header)
				child.SetFont(AppFonts::UiFont());
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
