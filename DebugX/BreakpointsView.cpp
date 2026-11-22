#include "pch.h"
#include "BreakpointsView.h"
#include "DebugManager.h"
#include "SortHelper.h"
#include "BreakpointDlg.h"

using namespace DebugEngine;

namespace {
	// "Code", or "Data: write 4"
	CString TypeText(const DebugEngine::BreakpointInfo& bp) {
		if (!bp.IsData)
			return L"Code";
		PCWSTR access = bp.DataAccess == BreakOnWrite ? L"write" : bp.DataAccess == BreakOnExecute ? L"execute" : L"read/write";
		CString text;
		text.Format(L"Data: %s %u", access, bp.DataSize);
		return text;
	}
}

void CBreakpointsView::InitList() {
	m_List.InsertColumn(0, L"ID", LVCFMT_RIGHT, 60);
	m_List.InsertColumn(1, L"Location", LVCFMT_LEFT, 280);
	m_List.InsertColumn(2, L"Address", LVCFMT_RIGHT, 140);
	m_List.InsertColumn(3, L"Type", LVCFMT_LEFT, 110);
	m_List.InsertColumn(4, L"Status", LVCFMT_LEFT, 80);
	m_List.InsertColumn(5, L"Command", LVCFMT_LEFT, 200);
	// (added after the others: the saved widths are by position)
	m_List.InsertColumn(6, L"Condition", LVCFMT_LEFT, 200);
	m_List.InsertColumn(7, L"Hits", LVCFMT_RIGHT, 50);
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
		case 3: return TypeText(bp);
		case 4:
			text = bp.Deferred ? L"Pending" : (bp.Enabled ? L"Enabled" : L"Disabled");
			if (bp.OneShot)
				text += L", one-shot";
			if (bp.PassCount > 1) {
				CString pass;
				pass.Format(L", on hit %u", bp.PassCount);
				text += pass;
			}
			if (bp.ThreadId != AnyId) {
				CString thread;
				thread.Format(L", thread %u", bp.ThreadId);
				text += thread;
			}
			break;
		case 5: return bp.Command.c_str();
		case 6: return bp.Condition.c_str();
		case 7: text.Format(L"%u", bp.Hits); break;
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
			case 6: return SortHelper::Sort(b1.Condition, b2.Condition, asc);
			case 7: return SortHelper::Sort(b1.Hits, b2.Hits, asc);
		}
		return false;
		});
}

void CBreakpointsView::OnListViewClick(HWND, int row, int col, POINT const& pt) {
	LVHITTESTINFO info{};
	info.pt = pt;
	if (m_List.HitTest(&info) >= 0 && (info.flags & LVHT_ONITEMSTATEICON) && info.iItem < static_cast<int>(m_Items.size())) {
		auto& bp = m_Items[info.iItem];
		SetEnabled({ bp.Id }, !bp.Enabled);
	}
}

bool CBreakpointsView::OnDoubleClickList(HWND, int row, int col, POINT const& pt) {
	if (row < 0)
		return false;

	EditSelected();
	return true;
}

bool CBreakpointsView::OnRightClickList(HWND, int row, int col, POINT const& pt) {
	// (a right-click on a row that is not selected selects it alone; one on a selected row keeps the selection)
	if (row >= 0 && !(m_List.GetItemState(row, LVIS_SELECTED) & LVIS_SELECTED))
		m_List.SelectItem(row);

	CMenu menu;
	menu.CreatePopupMenu();
	const UINT onRow = row >= 0 ? MF_STRING : (MF_STRING | MF_GRAYED);
	menu.AppendMenu(MF_STRING, ID_DEBUG_ADDBREAKPOINT, L"&Add...\tIns");
	menu.AppendMenu(onRow, ID_BREAKPOINT_EDIT, L"&Edit...\tEnter");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(onRow, ID_BREAKPOINT_TOGGLE, L"E&nable / Disable\tSpace");
	menu.AppendMenu(onRow, ID_BREAKPOINT_DELETE, L"&Delete\tDel");
	const UINT canChange = DebugManager::CanChangeBreakpoints() && !m_Items.empty() ? MF_STRING : MF_GRAYED;
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(canChange, ID_DEBUG_ENABLEALLBREAKPOINTS, L"Enable A&ll");
	menu.AppendMenu(canChange, ID_DEBUG_DISABLEALLBREAKPOINTS, L"Disable All");
	menu.AppendMenu(canChange, ID_DEBUG_DELETEALLBREAKPOINTS, L"Delete All\tCtrl+Shift+F9");
	if (row >= 0)
		menu.SetMenuDefaultItem(ID_BREAKPOINT_EDIT);
	AppendCopyItems(menu.m_hMenu);
	auto cmd = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd);
	if (HandleCopyCommand(cmd))
		return true;
	switch (cmd) {
		case ID_DEBUG_ADDBREAKPOINT: AddBreakpoint(); break;
		case ID_BREAKPOINT_EDIT: EditSelected(); break;
		case ID_BREAKPOINT_TOGGLE: ToggleSelected(); break;
		case ID_BREAKPOINT_DELETE: RemoveSelected(); break;
		case ID_DEBUG_ENABLEALLBREAKPOINTS: DebugManager::SetAllBreakpointsEnabled(true); break;
		case ID_DEBUG_DISABLEALLBREAKPOINTS: DebugManager::SetAllBreakpointsEnabled(false); break;
		case ID_DEBUG_DELETEALLBREAKPOINTS: DebugManager::RemoveAllBreakpoints(m_hWnd); break;
	}
	return true;
}

void CBreakpointsView::EditSelected() {
	const int row = m_List.GetNextItem(-1, LVNI_SELECTED);
	if (row < 0 || row >= static_cast<int>(m_Items.size()))
		return;

	// (a copy: the list may be made again while the dialog is up)
	auto bp = m_Items[row];
	CBreakpointDlg dlg(&bp);
	if (dlg.DoModal(m_hWnd) == IDOK)
		Update(bp.Id, dlg.GetSettings());
}

Task<void> CBreakpointsView::Update(ULONG id, BreakpointSettings settings) {
	auto alive = m_Lifetime.Watch();

	auto hr = co_await DebugManager::Debugger().UpdateBreakpoint(id, settings);
	if (!*alive)
		co_return;

	if (FAILED(hr))
		AtlMessageBox(*this, (PCWSTR)BreakpointErrorText(settings, hr), IDS_TITLE, MB_ICONERROR);
	// The engine says when a breakpoint changes, but not when only DebugX's own part of it (the condition) does
	RequestRefresh();
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
		case VK_RETURN: EditSelected(); break;
		default: handled = FALSE; break;
	}
	return 0;
}

// the frame has the command (and its dialog), also for the menu and Ctrl+B
void CBreakpointsView::AddBreakpoint() {
	::PostMessage(::GetAncestor(m_hWnd, GA_ROOTOWNER), WM_COMMAND, ID_DEBUG_ADDBREAKPOINT, 0);
}

// the selected rows' breakpoints (taken now: the list is made again as the engine says they change)
std::vector<DebugEngine::BreakpointInfo> CBreakpointsView::Selected() const {
	std::vector<DebugEngine::BreakpointInfo> selected;
	for (int row = m_List.GetNextItem(-1, LVNI_SELECTED); row >= 0; row = m_List.GetNextItem(row, LVNI_SELECTED))
		if (row < static_cast<int>(m_Items.size()))
			selected.push_back(m_Items[row]);
	return selected;
}

void CBreakpointsView::RemoveSelected() {
	if (!DebugManager::CanChangeBreakpoints())
		return;
	std::vector<ULONG> ids;
	for (auto& bp : Selected())
		ids.push_back(bp.Id);
	Remove(std::move(ids));
}

// Space: the selected ones all become what the first of them is not
void CBreakpointsView::ToggleSelected() {
	if (!DebugManager::CanChangeBreakpoints())
		return;
	auto selected = Selected();
	if (selected.empty())
		return;
	const bool enable = !selected[0].Enabled;
	std::vector<ULONG> ids;
	for (auto& bp : selected)
		if (bp.Enabled != enable)
			ids.push_back(bp.Id);
	SetEnabled(std::move(ids), enable);
}

// (the engine says when the breakpoints have changed, and the list is made again then)
DebugEngine::Task<void> CBreakpointsView::Remove(std::vector<ULONG> ids) {
	for (auto id : ids)
		co_await DebugManager::Debugger().RemoveBreakpoint(id);
}

DebugEngine::Task<void> CBreakpointsView::SetEnabled(std::vector<ULONG> ids, bool enabled) {
	for (auto id : ids)
		co_await DebugManager::Debugger().SetBreakpointEnabled(id, enabled);
}

void CBreakpointsView::OnTargetEnded() {
	m_Items.clear();
	UpdateList(0);
	MarkDirty();
}

DebugEngine::Task<void> CBreakpointsView::Refresh() {
	auto alive = m_Lifetime.Watch();

	std::vector<DebugEngine::BreakpointInfo> breakpoints;
	co_await DebugManager::Debugger().EnumBreakpoints(breakpoints);
	if (!*alive)
		co_return;

	m_Items.swap(breakpoints);
	UpdateList(m_Items.size());
	RefreshDone();
}
