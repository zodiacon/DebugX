#include "stdafx.h"
#include "BreakpointsView.h"
#include "DebugManager.h"
#include "SortHelper.h"

void CBreakpointsView::InitList() {
	m_List.InsertColumn(0, L"ID", LVCFMT_RIGHT, 60);
	m_List.InsertColumn(1, L"Location", LVCFMT_LEFT, 280);
	m_List.InsertColumn(2, L"Address", LVCFMT_RIGHT, 140);
	m_List.InsertColumn(3, L"Type", LVCFMT_LEFT, 60);
	m_List.InsertColumn(4, L"Status", LVCFMT_LEFT, 80);
	m_List.InsertColumn(5, L"Command", LVCFMT_LEFT, 240);
}

CString CBreakpointsView::GetColumnText(HWND hWnd, int row, int column) const {
	auto& bp = m_Items[row];
	CString text;

	switch (column) {
		case 0: text.Format(L"%u", bp.Id); break;
		case 1:
			if (!bp.Expression.empty())
				return bp.Expression.c_str();
			text.Format(L"0x%llX", bp.Offset);
			break;
		case 2:
			if (!bp.Deferred)
				text.Format(L"0x%016llX", bp.Offset);
			break;
		case 3: return bp.IsData ? L"Data" : L"Code";
		case 4: return bp.Deferred ? L"Pending" : (bp.Enabled ? L"Enabled" : L"Disabled");
		case 5: return bp.Command.c_str();
	}
	return text;
}

ListViewRowCheck CBreakpointsView::IsRowChecked(HWND, int row) const {
	return m_Items[row].Enabled ? ListViewRowCheck::Checked : ListViewRowCheck::Unchecked;
}

void CBreakpointsView::DoSort(const SortInfo* si) {
	if (si == nullptr)
		return;

	std::sort(m_Items.begin(), m_Items.end(), [&](const auto& b1, const auto& b2) {
		const bool asc = si->SortAscending;
		switch (si->SortColumn) {
			case 0: return SortHelper::Sort(b1.Id, b2.Id, asc);
			case 1: return SortHelper::Sort(b1.Expression, b2.Expression, asc);
			case 2: return SortHelper::Sort(b1.Offset, b2.Offset, asc);
			case 3: return SortHelper::Sort(b1.IsData, b2.IsData, asc);
			case 4: return SortHelper::Sort(b1.Enabled, b2.Enabled, asc);
			case 5: return SortHelper::Sort(b1.Command, b2.Command, asc);
		}
		return false;
		});
}

void CBreakpointsView::OnListViewClick(HWND, int row, int col, POINT const& pt) {
	LVHITTESTINFO info{};
	info.pt = pt;
	if (m_List.HitTest(&info) >= 0 && (info.flags & LVHT_ONITEMSTATEICON) && info.iItem < static_cast<int>(m_Items.size())) {
		auto& bp = m_Items[info.iItem];
		SetEnabled(bp.Id, !bp.Enabled);
	}
}

bool CBreakpointsView::OnDoubleClickList(HWND, int row, int col, POINT const& pt) {
	if (row < 0)
		return false;

	ToggleSelected();
	return true;
}

bool CBreakpointsView::OnRightClickList(HWND, int row, int col, POINT const& pt) {
	if (row >= 0)
		m_List.SelectItem(row);

	CMenu menu;
	menu.CreatePopupMenu();
	const UINT onRow = row >= 0 ? MF_STRING : (MF_STRING | MF_GRAYED);
	menu.AppendMenu(MF_STRING, ID_DEBUG_ADDBREAKPOINT, L"&Add...");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(onRow, ID_BREAKPOINT_TOGGLE, L"&Enable / Disable");
	menu.AppendMenu(onRow, ID_BREAKPOINT_DELETE, L"&Delete");
	switch (menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd)) {
		case ID_DEBUG_ADDBREAKPOINT: AddBreakpoint(); break;
		case ID_BREAKPOINT_TOGGLE: ToggleSelected(); break;
		case ID_BREAKPOINT_DELETE: RemoveSelected(); break;
	}
	return true;
}

LRESULT CBreakpointsView::OnKeyDown(int, LPNMHDR hdr, BOOL& handled) {
	if (hdr->hwndFrom != m_List.m_hWnd) {
		handled = FALSE;
		return 0;
	}

	switch (reinterpret_cast<LPNMLVKEYDOWN>(hdr)->wVKey) {
		case VK_DELETE: RemoveSelected(); break;
		case VK_INSERT: AddBreakpoint(); break;
		case VK_SPACE: ToggleSelected(); break;
		default: handled = FALSE; break;
	}
	return 0;
}

// the frame has the command (and its dialog), also for the menu and F9
void CBreakpointsView::AddBreakpoint() {
	::PostMessage(::GetAncestor(m_hWnd, GA_ROOTOWNER), WM_COMMAND, ID_DEBUG_ADDBREAKPOINT, 0);
}

// (the first selected row; CListViewCtrl::GetSelectedIndex is only for lists that allow a single selection)
void CBreakpointsView::RemoveSelected() {
	const int row = m_List.GetNextItem(-1, LVNI_SELECTED);
	if (row >= 0 && row < static_cast<int>(m_Items.size()))
		Remove(m_Items[row].Id);
}

void CBreakpointsView::ToggleSelected() {
	const int row = m_List.GetNextItem(-1, LVNI_SELECTED);
	if (row >= 0 && row < static_cast<int>(m_Items.size()))
		SetEnabled(m_Items[row].Id, !m_Items[row].Enabled);
}

// (the engine says when the breakpoints have changed, and the list is made again then)
DebugEngine::Task<void> CBreakpointsView::Remove(ULONG id) {
	co_await DebugManager::DebuggerX().RemoveBreakpoint(id);
}

DebugEngine::Task<void> CBreakpointsView::SetEnabled(ULONG id, bool enabled) {
	co_await DebugManager::DebuggerX().SetBreakpointEnabled(id, enabled);
}

void CBreakpointsView::OnTargetEnded() {
	m_Items.clear();
	UpdateList(0);
	MarkDirty();
}

DebugEngine::Task<void> CBreakpointsView::Refresh() {
	auto alive = m_Lifetime.Watch();

	std::vector<DebugEngine::BreakpointInfo> breakpoints;
	co_await DebugManager::DebuggerX().EnumBreakpoints(breakpoints);
	if (!*alive)
		co_return;

	m_Items.swap(breakpoints);
	UpdateList(m_Items.size());
	RefreshDone();
}
