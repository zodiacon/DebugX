#include "pch.h"
#include "HandlesView.h"
#include "DebugManager.h"
#include "SortHelper.h"
#include "ClipboardHelper.h"
#include <format>

void CHandlesView::InitList() {
	m_List.InsertColumn(0, L"Handle", LVCFMT_RIGHT, 80);
	m_List.InsertColumn(1, L"Type", LVCFMT_LEFT, 120);
	m_List.InsertColumn(2, L"Name", LVCFMT_LEFT, 360);
	m_List.InsertColumn(3, L"Access", LVCFMT_RIGHT, 90);
	m_List.InsertColumn(4, L"Attributes", LVCFMT_LEFT, 90);
	m_List.InsertColumn(5, L"Object", LVCFMT_RIGHT, 140);
}

CString CHandlesView::GetColumnText(HWND, int row, int column) const {
	auto& h = m_Items[row];
	CString text;
	switch (column) {
		case 0: text.Format(L"0x%llX", h.Handle); break;
		case 1: text = h.Type.c_str(); break;
		case 2: text = h.Name.c_str(); break;
		case 3: text.Format(L"0x%08X", h.GrantedAccess); break;
		case 4:
			if (h.Attributes & 2)		// OBJ_INHERIT
				text = L"Inherit";
			if (h.Attributes & 1)		// OBJ_PROTECT_CLOSE
				text += text.IsEmpty() ? L"Protect" : L", Protect";
			break;
		case 5:
			if (h.Object)
				text.Format(L"0x%llX", h.Object);
			break;
	}
	return text;
}

void CHandlesView::DoSort(const SortInfo* si) {
	if (si == nullptr)
		return;
	std::sort(m_Items.begin(), m_Items.end(), [&](const auto& h1, const auto& h2) {
		switch (si->SortColumn) {
			case 0: return SortHelper::Sort(h1.Handle, h2.Handle, si->SortAscending);
			case 1: return SortHelper::Sort(h1.Type, h2.Type, si->SortAscending);
			case 2: return SortHelper::Sort(h1.Name, h2.Name, si->SortAscending);
			case 3: return SortHelper::Sort(h1.GrantedAccess, h2.GrantedAccess, si->SortAscending);
			case 4: return SortHelper::Sort(h1.Attributes, h2.Attributes, si->SortAscending);
			case 5: return SortHelper::Sort(h1.Object, h2.Object, si->SortAscending);
		}
		return false;
		});
}

bool CHandlesView::OnRightClickList(HWND, int row, int, POINT const& pt) {
	if (row < 0 || row >= static_cast<int>(m_Items.size()))
		return false;
	m_List.SelectItem(row);
	auto name = m_Items[row].Name;
	auto handle = m_Items[row].Handle;

	enum { CmdDetails = 1, CmdCopyName };
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, CmdDetails, L"&Details (!handle)");
	menu.AppendMenu(name.empty() ? MF_STRING | MF_GRAYED : MF_STRING, CmdCopyName, L"&Copy Name");
	switch (menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd)) {
		case CmdDetails:
			DebugManager::DebuggerX().ExecuteCommand(std::format(L"!handle 0x{:X} f", handle).c_str());
			break;
		case CmdCopyName:
			ClipboardHelper::CopyText(m_hWnd, name.c_str());
			break;
	}
	return true;
}

void CHandlesView::OnTargetEnded() {
	m_Items.clear();
	UpdateList(0);
	MarkDirty();
}

bool CHandlesView::CanRefresh() const {
	auto& session = DebugManager::Session();
	return session.IsBroken() && session.Target() == DebugEngine::TargetType::LiveUser;
}

DebugEngine::Task<void> CHandlesView::Refresh() {
	auto alive = m_Lifetime.Watch();
	std::vector<DebugEngine::HandleInfo> handles;
	co_await DebugManager::DebuggerX().EnumHandles(handles);
	if (!*alive)
		co_return;
	m_Items.swap(handles);
	UpdateList(m_Items.size());
	RefreshDone();
}
