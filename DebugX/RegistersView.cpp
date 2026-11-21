#include "pch.h"
#include "RegistersView.h"
#include "DebugManager.h"
#include "SortHelper.h"

using namespace DebugEngine;

void CRegistersView::InitList() {
	m_List.InsertColumn(0, L"Register", LVCFMT_LEFT, 90);
	m_List.InsertColumn(1, L"Value", LVCFMT_LEFT, 320);

	// the value editor: made once, shown over the value being edited
	m_Editor.Create(m_List, rcDefault, nullptr, WS_CHILD | WS_BORDER | ES_AUTOHSCROLL);
	m_Editor.SetFont(m_List.GetFont());
}

bool CRegistersView::CanEdit() const {
	auto& session = DebugManager::Session();
	return session.IsBroken() && session.CurrentFrame() == 0 && (session.Target() & TargetType::Live) == TargetType::Live;
}

bool CRegistersView::OnDoubleClickList(HWND, int row, int, POINT const&) {
	if (row < 0)
		return false;
	BeginEdit(row);
	return true;
}

void CRegistersView::BeginEdit(int row) {
	if (row < 0 || row >= static_cast<int>(m_Items.size()) || !CanEdit()) {
		::MessageBeep(MB_ICONWARNING);
		return;
	}

	CRect rc;
	if (!m_List.GetSubItemRect(row, 1, LVIR_LABEL, &rc))
		return;

	m_List.EnsureVisible(row, FALSE);
	m_List.GetSubItemRect(row, 1, LVIR_LABEL, &rc);
	m_EditName = m_Items[row].Name;
	m_EditValue = m_Items[row].Value;
	m_Editing = true;
	m_Editor.SetWindowText(m_EditValue.c_str());
	m_Editor.MoveWindow(rc.left, rc.top - 1, rc.Width(), rc.Height() + 2);
	m_Editor.ShowWindow(SW_SHOW);
	m_Editor.SetFocus();
	m_Editor.SetSelAll();
}

void CRegistersView::EndEdit(bool commit) {
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
		m_List.SetFocus();

	if (commit && !text.IsEmpty() && m_EditValue != (PCWSTR)text)
		SetRegister(m_EditName, (PCWSTR)text);
}

Task<void> CRegistersView::SetRegister(std::wstring name, std::wstring value) {
	auto alive = m_Lifetime.Watch();

	// (the panes that show the thread hear of the change from the engine)
	auto hr = co_await DebugManager::DebuggerX().SetRegister(name.c_str(), value.c_str());
	if (!*alive)
		co_return;

	if (FAILED(hr)) {
		CString text;
		if (hr == E_NOTIMPL)
			text.Format(L"%s cannot be changed here (only integer and floating-point registers can)", name.c_str());
		else
			text.Format(L"Failed to set %s to \"%s\" (0x%08X)", name.c_str(), value.c_str(), static_cast<unsigned>(hr));
		AtlMessageBox(*this, (PCWSTR)text, IDS_TITLE, MB_ICONERROR);
	}
}

LRESULT CRegistersView::OnListKeyDown(int, LPNMHDR hdr, BOOL& handled) {
	auto vk = reinterpret_cast<LPNMLVKEYDOWN>(hdr)->wVKey;
	if (hdr->hwndFrom != m_List.m_hWnd || (vk != VK_F2 && vk != VK_RETURN)) {
		handled = FALSE;
		return 0;
	}
	BeginEdit(m_List.GetNextItem(-1, LVNI_SELECTED));
	return 0;
}

LRESULT CRegistersView::OnListScroll(int, LPNMHDR hdr, BOOL& handled) {
	// (the editor would not move with its row)
	if (hdr->hwndFrom == m_List.m_hWnd)
		EndEdit(true);
	handled = FALSE;
	return 0;
}

LRESULT CRegistersView::OnEditorKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& handled) {
	switch (wParam) {
		case VK_RETURN: EndEdit(true); return 0;
		case VK_ESCAPE: EndEdit(false); return 0;
	}
	handled = FALSE;
	return 0;
}

LRESULT CRegistersView::OnEditorKillFocus(UINT, WPARAM, LPARAM, BOOL& handled) {
	EndEdit(true);
	handled = FALSE;
	return 0;
}

LRESULT CRegistersView::OnEditorGetDlgCode(UINT, WPARAM, LPARAM, BOOL&) {
	// Enter and Escape are the editor's, not the dock's or a dialog's
	return DLGC_WANTALLKEYS;
}

CString CRegistersView::GetColumnText(HWND hWnd, int row, int column) const {
	auto& r = m_Items[row];
	return (column == 0 ? r.Name : r.Value).c_str();
}

void CRegistersView::DoSort(const SortInfo* si) {
	if (si == nullptr)
		return;

	std::sort(m_Items.begin(), m_Items.end(), [&](const auto& r1, const auto& r2) {
		return si->SortColumn == 0 ? SortHelper::Sort(r1.Name, r2.Name, si->SortAscending) : SortHelper::Sort(r1.Value, r2.Value, si->SortAscending);
		});
}

void CRegistersView::OnTargetEnded() {
	EndEdit(false);
	m_Items.clear();
	m_Previous.clear();
	m_LastFrame0.clear();
	m_PreviousThread = 0;
	m_Resumed = true;
	UpdateList(0);
	MarkDirty();
}

DebugEngine::Task<void> CRegistersView::Refresh() {
	auto alive = m_Lifetime.Watch();
	// (the rows are about to change under it)
	EndEdit(false);

	std::vector<DebugEngine::RegisterInfo> registers;
	co_await DebugManager::DebuggerX().GetRegisters(registers);
	if (!*alive)
		co_return;

	// What is compared with is the state at the last stop, not at the last refresh: the same stop can be looked at more
	// than once, and the changes it made have to stay in red. Another thread has nothing in common with this one.
	// (another frame's registers are not the thread's at the stop, and are never what is compared with)
	const auto thread = DebugManager::Session().CurrentThreadId();
	const auto frame0 = DebugManager::Session().CurrentFrame() == 0;
	if (thread != m_PreviousThread) {
		m_Previous.clear();
		m_LastFrame0.clear();
		m_PreviousThread = thread;
		m_Resumed = false;
	}
	else if (m_Resumed) {
		m_Previous = m_LastFrame0;
		m_Resumed = false;
	}

	m_Items.clear();
	m_Items.reserve(registers.size());
	if (frame0)
		m_LastFrame0.clear();
	for (auto& r : registers) {
		auto it = m_Previous.find(r.Name);
		const bool changed = frame0 && it != m_Previous.end() && it->second != r.Value;
		if (frame0)
			m_LastFrame0[r.Name] = r.Value;
		m_Items.push_back({ std::move(r.Name), std::move(r.Value), changed });
	}
	UpdateList(m_Items.size());
	RefreshDone();
}

LRESULT CRegistersView::OnCustomDraw(int, LPNMHDR hdr, BOOL& handled) {
	if (hdr->hwndFrom != m_List.m_hWnd) {
		handled = FALSE;
		return 0;
	}

	auto cd = reinterpret_cast<LPNMLVCUSTOMDRAW>(hdr);
	switch (cd->nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;

		case CDDS_ITEMPREPAINT: {
			const auto row = static_cast<size_t>(cd->nmcd.dwItemSpec);
			if (row < m_Items.size() && m_Items[row].Changed) {
				cd->clrText = WTLHelper::IsDarkMode() ? RGB(255, 110, 110) : RGB(200, 0, 0);
				return CDRF_NEWFONT;
			}
			break;
		}
	}
	return CDRF_DODEFAULT;
}
