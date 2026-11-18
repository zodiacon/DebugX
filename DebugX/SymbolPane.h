#pragma once

#include <set>
#include "ListPane.h"

//
// A pane that shows variables as a tree (SymbolEntry rows: Locals, Watch): name, value, type, children indented under
// their parent. A row with children has a triangle; it opens and closes with a click on it, a double click, Enter,
// or Right/Left. What is open is kept by path (SymbolEntry::Path), so it stays open across refreshes -- the engine
// is asked for the tree with those paths expanded.
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
		CHAIN_MSG_MAP(CListPane<T>)
	END_MSG_MAP()

	void InitList() {
		this->m_List.InsertColumn(0, L"Name", LVCFMT_LEFT, 240);
		this->m_List.InsertColumn(1, L"Value", LVCFMT_LEFT, 280);
		this->m_List.InsertColumn(2, L"Type", LVCFMT_LEFT, 200);
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

	bool OnDoubleClickList(HWND, int row, int, POINT const&) {
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

	// CPaneBase
	DebugEngine::Task<void> Refresh() {
		auto alive = this->m_Lifetime.Watch();

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

	std::vector<DebugEngine::SymbolEntry> m_Entries;
	std::set<std::wstring> m_Expanded;

private:
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
