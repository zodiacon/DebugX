#include "stdafx.h"
#include "UserProcessesView.h"
#include "DebugManager.h"
#include "SortHelper.h"

void CUserProcessesView::InitList() {
	m_List.InsertColumn(0, L"Index", LVCFMT_LEFT, 60);
	m_List.InsertColumn(1, L"PID", LVCFMT_RIGHT, 110);
	m_List.InsertColumn(2, L"Image", LVCFMT_LEFT, 260);
	m_List.InsertColumn(3, L"Threads", LVCFMT_RIGHT, 60);
	m_List.InsertColumn(4, L"PEB", LVCFMT_RIGHT, 130);
	m_List.InsertColumn(5, L"Up Time", LVCFMT_RIGHT, 80);

	CImageList images;
	images.Create(16, 16, ILC_COLOR32, 2, 0);
	images.AddIcon(AtlLoadIconImage(IDI_PROCESS, 0, 16, 16));
	images.AddIcon(AtlLoadIconImage(IDI_ICON9, 0, 16, 16));
	m_List.SetImageList(images, LVSIL_SMALL);
}

CString CUserProcessesView::GetColumnText(HWND, int row, int column) const {
	auto& p = m_Items[row];
	CString text;
	switch (column) {
		case 0: text.Format(L"%u", p.Id); break;
		case 1: text.Format(L"%u (0x%X)", p.ProcessId, p.ProcessId); break;
		case 2: text = p.ImagePath.c_str(); break;
		case 3: text.Format(L"%u", p.ThreadCount); break;
		case 4: text.Format(L"0x%llX", p.Peb); break;
		case 5: text.Format(L"%u:%02u:%02u", p.UpTimeSec / 3600, p.UpTimeSec / 60 % 60, p.UpTimeSec % 60); break;
	}
	return text;
}

int CUserProcessesView::GetRowImage(HWND, int row, int) const {
	return m_Items[row].Current ? 1 : 0;
}

void CUserProcessesView::DoSort(const SortInfo* si) {
	if (si == nullptr)
		return;
	std::sort(m_Items.begin(), m_Items.end(), [&](const auto& p1, const auto& p2) {
		switch (si->SortColumn) {
			case 0: return SortHelper::Sort(p1.Id, p2.Id, si->SortAscending);
			case 1: return SortHelper::Sort(p1.ProcessId, p2.ProcessId, si->SortAscending);
			case 2: return SortHelper::Sort(p1.ImagePath, p2.ImagePath, si->SortAscending);
			case 3: return SortHelper::Sort(p1.ThreadCount, p2.ThreadCount, si->SortAscending);
			case 4: return SortHelper::Sort(p1.Peb, p2.Peb, si->SortAscending);
			case 5: return SortHelper::Sort(p1.UpTimeSec, p2.UpTimeSec, si->SortAscending);
		}
		return false;
		});
}

bool CUserProcessesView::OnDoubleClickList(HWND, int row, int, POINT const&) {
	if (row < 0 || row >= static_cast<int>(m_Items.size()))
		return false;
	Apply(m_Items[row].Id, Action::Switch);
	return true;
}

bool CUserProcessesView::OnRightClickList(HWND, int row, int, POINT const& pt) {
	if (row < 0 || row >= static_cast<int>(m_Items.size()))
		return false;
	m_List.SelectItem(row);
	const auto id = m_Items[row].Id;
	const bool live = (DebugManager::Session().Target() & DebugEngine::TargetType::Live) == DebugEngine::TargetType::Live;

	enum { CmdSwitch = 1, CmdDetach, CmdTerminate };
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, CmdSwitch, L"&Switch to Process");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(live ? MF_STRING : MF_STRING | MF_GRAYED, CmdDetach, L"&Detach (.detach)");
	menu.AppendMenu(live ? MF_STRING : MF_STRING | MF_GRAYED, CmdTerminate, L"&Terminate (.kill)");
	menu.SetMenuDefaultItem(CmdSwitch);
	switch (menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd)) {
		case CmdSwitch: Apply(id, Action::Switch); break;
		case CmdDetach: Apply(id, Action::Detach); break;
		case CmdTerminate: Apply(id, Action::Terminate); break;
	}
	return true;
}

DebugEngine::Task<void> CUserProcessesView::Apply(ULONG processId, Action action) {
	auto alive = m_Lifetime.Watch();
	auto& dbg = DebugManager::DebuggerX();
	// (.detach and .kill are about the current process: it becomes the one first)
	auto hr = co_await dbg.SetCurrentProcess(processId);
	if (!*alive)
		co_return;
	if (SUCCEEDED(hr) && action != Action::Switch)
		hr = co_await dbg.ExecuteCommand(action == Action::Detach ? L".detach" : L".kill");
	if (!*alive)
		co_return;
	if (FAILED(hr))
		::MessageBeep(MB_ICONWARNING);
	RequestRefresh();
}

void CUserProcessesView::OnTargetEnded() {
	m_Items.clear();
	UpdateList(0);
	MarkDirty();
}

bool CUserProcessesView::CanRefresh() const {
	auto& session = DebugManager::Session();
	return session.IsBroken() && (session.Target() & DebugEngine::TargetType::User) == DebugEngine::TargetType::User;
}

DebugEngine::Task<void> CUserProcessesView::Refresh() {
	auto alive = m_Lifetime.Watch();
	std::vector<DebugEngine::ProcessInfo> processes;
	std::vector<DebugEngine::ThreadInfo> threads;
	co_await DebugManager::DebuggerX().EnumProcesses(processes, threads);
	if (!*alive)
		co_return;
	m_Items.swap(processes);
	UpdateList(m_Items.size());
	RefreshDone();
}
