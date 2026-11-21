#pragma once

#include <functional>
#include <set>
#include "ListPane.h"
#include "resource.h"

// The box a value is edited in, over its cell; Enter (or moving away) ends the edit with the value, Escape without.
class CValueEditor : public CWindowImpl<CValueEditor, CEdit> {
public:
	DECLARE_WND_SUPERCLASS(nullptr, CEdit::GetWndClassName())

	std::function<void(bool commit)> OnEnd;

	BEGIN_MSG_MAP(CValueEditor)
		MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnKillFocus)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnGetDlgCode)
	END_MSG_MAP()

private:
	LRESULT OnKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& handled) {
		if (wParam == VK_RETURN || wParam == VK_ESCAPE) {
			if (OnEnd)
				OnEnd(wParam == VK_RETURN);
			return 0;
		}
		handled = FALSE;
		return 0;
	}
	LRESULT OnKillFocus(UINT, WPARAM, LPARAM, BOOL& handled) {
		if (OnEnd)
			OnEnd(true);
		handled = FALSE;
		return 0;
	}
	LRESULT OnGetDlgCode(UINT, WPARAM, LPARAM, BOOL&) {
		// Enter and Escape are the editor's, not the dock's or a dialog's
		return DLGC_WANTALLKEYS;
	}
};

//
// A pane that shows variables as a tree (SymbolEntry rows: Locals, Watch): name, value, type, children indented under
// their parent. A row with children has a triangle; it opens and closes with a click on it, a double click, Enter,
// or Right/Left. What is open is kept by path (SymbolEntry::Path), so it stays open across refreshes -- the engine
// is asked for the tree with those paths expanded.
//
// A value that has an expression (SymbolEntry::Expression: a number, a pointer, ...) can be changed in place, in a
// live target that is stopped: double-click it (the Value column), or Edit Value (F2 in Locals); what is typed is a
// C++ expression, assigned with IDebugger::AssignValue.
//
//   class CMyPane : public CSymbolPane<CMyPane> {
//       DebugEngine::Task<HRESULT> Fetch(std::vector<DebugEngine::SymbolEntry>& entries);    // the tree, now
//   };
//
template<typename T>
class CSymbolPane : public CListPane<T> {
public:
	BEGIN_MSG_MAP(CSymbolPane)
		NOTIFY_CODE_HANDLER(LVN_KEYDOWN, OnListKeyDown)
		NOTIFY_CODE_HANDLER(LVN_BEGINSCROLL, OnListScroll)
		CHAIN_MSG_MAP(CListPane<T>)
	END_MSG_MAP()

	void InitList() {
		this->m_List.InsertColumn(0, L"Name", LVCFMT_LEFT, 240);
		this->m_List.InsertColumn(1, L"Value", LVCFMT_LEFT, 280);
		this->m_List.InsertColumn(2, L"Type", LVCFMT_LEFT, 200);

		// the value editor: made once, shown over the value being edited
		m_Editor.Create(this->m_List, CWindow::rcDefault, nullptr, WS_CHILD | WS_BORDER | ES_AUTOHSCROLL);
		m_Editor.SetFont(this->m_List.GetFont());
		m_Editor.OnEnd = [this](bool commit) { EndEdit(commit); };
	}

	CString GetColumnText(HWND, int row, int column) const {
		if (row < 0 || row >= static_cast<int>(m_Entries.size()))
			return L"";

		auto& e = m_Entries[row];
		switch (column) {
			case 0: return Indent(e) + e.Name.c_str();
			case 1: return e.Value.c_str();
			case 2: return e.Type.c_str();
		}
		return L"";
	}
	int GetRowImage(HWND, int, int) const {
		return -1;
	}
	bool IsSortable(HWND, int) const {
		return false;
	}

	bool OnDoubleClickList(HWND, int row, int col, POINT const&) {
		if (col == 1 && CanEditValue(row)) {
			BeginEdit(row);
			return true;
		}
		return Toggle(row);
	}

	// a click on the triangle
	void OnListViewClick(HWND, int row, int col, POINT const& pt) {
		if (col != 0 || row < 0 || row >= static_cast<int>(m_Entries.size()) || !m_Entries[row].Expandable)
			return;

		CRect rc;
		if (!this->m_List.GetSubItemRect(row, 0, LVIR_LABEL, &rc))
			return;
		auto& e = m_Entries[row];
		const int indent = this->m_List.GetStringWidth(CString(L' ', IndentWidth * e.Depth));
		const int glyph = this->m_List.GetStringWidth(Glyph(e));
		const int x = pt.x - rc.left;
		if (x >= indent && x <= indent + glyph + 6)
			Toggle(row);
	}

	// ISessionObserver: another thread, another frame
	void OnContextChanged() override {
		this->RequestRefresh();
	}
	void OnScopeChanged() override {
		this->RequestRefresh();
	}
	void OnMemoryChanged() override {
		this->RequestRefresh();
	}
	void OnResumed() override {
		EndEdit(false);
	}

	// CPaneBase
	DebugEngine::Task<void> Refresh() {
		auto alive = this->m_Lifetime.Watch();
		// (the rows are about to change under it)
		EndEdit(false);

		std::vector<DebugEngine::SymbolEntry> entries;
		co_await static_cast<T*>(this)->Fetch(entries);
		if (!*alive)
			co_return;

		SetEntries(std::move(entries));
		this->RefreshDone();
	}

protected:
	// ...in the derived pane, if it has rows of its own after the entries
	size_t RowCount() const {
		return m_Entries.size();
	}

	// the rows to show, keeping the selected one (by path) selected
	void SetEntries(std::vector<DebugEngine::SymbolEntry> entries) {
		std::wstring selected;
		const int sel = this->m_List.GetNextItem(-1, LVNI_SELECTED);
		if (sel >= 0 && sel < static_cast<int>(m_Entries.size()))
			selected = m_Entries[sel].Path;

		m_Entries = std::move(entries);
		this->UpdateList(static_cast<T*>(this)->RowCount());

		if (!selected.empty()) {
			for (size_t i = 0; i < m_Entries.size(); i++) {
				if (m_Entries[i].Path == selected) {
					this->m_List.SetItemState(-1, 0, LVIS_SELECTED);
					this->m_List.SetItemState(static_cast<int>(i), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
					break;
				}
			}
		}
	}

	// opens or closes the row; false if it has no children
	bool Toggle(int row) {
		if (row < 0 || row >= static_cast<int>(m_Entries.size()) || !m_Entries[row].Expandable)
			return false;

		SetExpanded(m_Entries[row].Path, !m_Entries[row].Expanded);
		return true;
	}

	void SetExpanded(const std::wstring& path, bool expand) {
		if (expand)
			m_Expanded.insert(path);
		else
			m_Expanded.erase(path);
		this->RequestRefresh();
	}

	std::vector<std::wstring> ExpandedPaths() const {
		return { m_Expanded.begin(), m_Expanded.end() };
	}

	// for keys a pane has a use of its own for; true if it handled it
	bool OnKey(UINT /*vk*/, int /*row*/) {
		return false;
	}

	// the row's value can be changed: it has an expression, and the target is live and stopped
	bool CanEditValue(int row) const {
		if (row < 0 || row >= static_cast<int>(m_Entries.size()) || m_Entries[row].Expression.empty() || m_Entries[row].Error)
			return false;
		auto& session = DebugManager::Session();
		return session.IsBroken() && (session.Target() & DebugEngine::TargetType::Live) == DebugEngine::TargetType::Live;
	}

	void BeginEdit(int row) {
		if (!CanEditValue(row)) {
			::MessageBeep(MB_ICONWARNING);
			return;
		}
		this->m_List.EnsureVisible(row, FALSE);
		CRect rc;
		if (!this->m_List.GetSubItemRect(row, 1, LVIR_LABEL, &rc))
			return;

		m_EditExpression = m_Entries[row].Expression;
		// (what is shown can have more after the number: 65 'A', 0x1234 "text" -- the number is what is edited)
		auto& value = m_Entries[row].Value;
		m_EditValue = value.substr(0, value.find(L' '));
		m_Editing = true;
		m_Editor.SetWindowText(m_EditValue.c_str());
		m_Editor.MoveWindow(rc.left, rc.top - 1, rc.Width(), rc.Height() + 2);
		m_Editor.ShowWindow(SW_SHOW);
		m_Editor.SetFocus();
		m_Editor.SetSelAll();
	}

	// 'commit': what was typed is assigned (if it is not what was there)
	void EndEdit(bool commit) {
		if (!m_Editing)
			return;

		// (first: hiding the editor takes the focus from it, and that comes back here)
		m_Editing = false;
		CString text;
		m_Editor.GetWindowText(text);
		text.Trim();
		const bool hadFocus = ::GetFocus() == m_Editor.m_hWnd;
		m_Editor.ShowWindow(SW_HIDE);
		if (hadFocus)
			this->m_List.SetFocus();

		if (commit && !text.IsEmpty() && m_EditValue != (PCWSTR)text)
			Assign(m_EditExpression, (PCWSTR)text);
	}

	std::vector<DebugEngine::SymbolEntry> m_Entries;
	std::set<std::wstring> m_Expanded;

private:
	DebugEngine::Task<void> Assign(std::wstring expression, std::wstring value) {
		auto alive = this->m_Lifetime.Watch();
		// (the panes hear of the change: MemoryChanged)
		std::wstring error;
		auto hr = co_await DebugManager::Debugger().AssignValue(expression.c_str(), value.c_str(), error);
		if (!*alive)
			co_return;
		if (FAILED(hr)) {
			CString text;
			text.Format(L"Cannot set %s to %s: %s", expression.c_str(), value.c_str(), error.c_str());
			AtlMessageBox(*static_cast<T*>(this), (PCWSTR)text, IDS_TITLE, MB_ICONERROR);
		}
	}

	LRESULT OnListScroll(int, LPNMHDR hdr, BOOL& handled) {
		// (the editor would not move with its row)
		if (hdr->hwndFrom == this->m_List.m_hWnd)
			EndEdit(true);
		handled = FALSE;
		return 0;
	}

	CValueEditor m_Editor;
	std::wstring m_EditExpression, m_EditValue;		// what is being edited, and what it was
	bool m_Editing{ false };

	static constexpr int IndentWidth = 4;

	static PCWSTR Glyph(const DebugEngine::SymbolEntry& e) {
		return e.Expandable ? (e.Expanded ? L"\x25BE " : L"\x25B8 ") : L"   ";
	}
	static CString Indent(const DebugEngine::SymbolEntry& e) {
		return CString(L' ', IndentWidth * e.Depth) + Glyph(e);
	}

	LRESULT OnListKeyDown(int, LPNMHDR hdr, BOOL& handled) {
		if (hdr->hwndFrom != this->m_List.m_hWnd) {
			handled = FALSE;
			return 0;
		}

		const auto vk = reinterpret_cast<LPNMLVKEYDOWN>(hdr)->wVKey;
		const int row = this->m_List.GetNextItem(-1, LVNI_SELECTED);
		if (static_cast<T*>(this)->OnKey(vk, row))
			return 0;

		const bool valid = row >= 0 && row < static_cast<int>(m_Entries.size());
		switch (vk) {
			case VK_RETURN:
				Toggle(row);
				break;

			case VK_RIGHT:
				if (valid && m_Entries[row].Expandable && !m_Entries[row].Expanded)
					SetExpanded(m_Entries[row].Path, true);
				break;

			case VK_LEFT:
				if (!valid)
					break;
				if (m_Entries[row].Expanded)
					SetExpanded(m_Entries[row].Path, false);
				else if (m_Entries[row].Depth > 0) {
					// to the parent
					for (int i = row - 1; i >= 0; i--) {
						if (m_Entries[i].Depth < m_Entries[row].Depth) {
							this->m_List.SetItemState(-1, 0, LVIS_SELECTED);
							this->m_List.SelectItem(i);
							break;
						}
					}
				}
				break;

			default:
				handled = FALSE;
				break;
		}
		return 0;
	}
};
