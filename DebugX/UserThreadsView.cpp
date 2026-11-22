#include "pch.h"
#include "UserThreadsView.h"
#include "DebugManager.h"
#include "SortHelper.h"

void CUserThreadsView::InitList() {
	m_List.InsertColumn(0, L"Index", LVCFMT_LEFT, 60);
	m_List.InsertColumn(1, L"TID", LVCFMT_RIGHT, 110);
	m_List.InsertColumn(2, L"Name", LVCFMT_LEFT, 120);
	m_List.InsertColumn(3, L"Location", LVCFMT_LEFT, 260);
	m_List.InsertColumn(4, L"Suspend", LVCFMT_RIGHT, 60);
	m_List.InsertColumn(5, L"State", LVCFMT_LEFT, 60);
	m_List.InsertColumn(6, L"PID", LVCFMT_RIGHT, 110);
	m_List.InsertColumn(7, L"TEB", LVCFMT_RIGHT, 130);

	// the thread, and the thread that is current
	CImageList images;
	images.Create(16, 16, ILC_COLOR32, 2, 0);
	images.AddIcon(AtlLoadIconImage(IDI_THREADS, 0, 16, 16));
	images.AddIcon(AtlLoadIconImage(IDI_ICON9, 0, 16, 16));
	m_List.SetImageList(images, LVSIL_SMALL);
}

CString CUserThreadsView::GetColumnText(HWND hWnd, int row, int column) const {
	auto& t = m_Items[row];
	CString text;

	switch (column) {
		case 0: text.Format(L"%u", t.Id); break;
		case 1: text.Format(L"%u (0x%X)", t.ThreadId, t.ThreadId); break;
		case 2: text = t.Name.c_str(); break;
		case 3: text = t.Location.c_str(); break;
		case 4: text.Format(L"%u", t.SuspendCount); break;
		case 5: text = t.Frozen ? L"Frozen" : L""; break;
		case 6: text.Format(L"%u (0x%X)", t.Pid, t.Pid); break;
		case 7: text.Format(L"0x%llX", t.Teb); break;
	}

	return text;
}

int CUserThreadsView::GetRowImage(HWND, int row, int col) const {
	return m_Items[row].Current ? 1 : 0;
}

void CUserThreadsView::DoSort(const SortInfo* si) {
	if (si == nullptr)
		return;

	std::sort(m_Items.begin(), m_Items.end(), [&](const auto& t1, const auto& t2) {
		switch (si->SortColumn) {
			case 0: return SortHelper::Sort(t1.Id, t2.Id, si->SortAscending);
			case 1: return SortHelper::Sort(t1.ThreadId, t2.ThreadId, si->SortAscending);
			case 2: return SortHelper::Sort(t1.Name, t2.Name, si->SortAscending);
			case 3: return SortHelper::Sort(t1.Location, t2.Location, si->SortAscending);
			case 4: return SortHelper::Sort(t1.SuspendCount, t2.SuspendCount, si->SortAscending);
			case 5: return SortHelper::Sort(t1.Frozen, t2.Frozen, si->SortAscending);
			case 6: return SortHelper::Sort(t1.Pid, t2.Pid, si->SortAscending);
			case 7: return SortHelper::Sort(t1.Teb, t2.Teb, si->SortAscending);
		}
		return false;
		});
}

bool CUserThreadsView::OnDoubleClickList(HWND, int row, int col, POINT const& pt) {
	if (row < 0 || row >= static_cast<int>(m_Items.size()))
		return false;

	SwitchTo(m_Items[row].Id);
	return true;
}

bool CUserThreadsView::OnRightClickList(HWND, int row, int, POINT const& pt) {
	if (row < 0 || row >= static_cast<int>(m_Items.size()))
		return false;
	m_List.SelectItem(row);
	auto& t = m_Items[row];
	// (a dump's threads are as they were: nothing to do to them)
	const bool live = DebugManager::Session().IsBroken() && (DebugManager::Session().Target() & DebugEngine::TargetType::Live) == DebugEngine::TargetType::Live;
	auto flags = [&](bool enabled) { return enabled ? MF_STRING : MF_STRING | MF_GRAYED; };

	enum { CmdSwitch = 1, CmdFreeze, CmdThaw, CmdSuspend, CmdResume, CmdCopy };
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, CmdSwitch, L"&Switch to Thread");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(flags(live && !t.Frozen), CmdFreeze, L"&Freeze");
	menu.AppendMenu(flags(live && t.Frozen), CmdThaw, L"&Thaw");
	menu.AppendMenu(flags(live), CmdSuspend, L"S&uspend");
	menu.AppendMenu(flags(live && t.SuspendCount > 0), CmdResume, L"&Resume");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, CmdCopy, L"&Copy Location");
	menu.SetMenuDefaultItem(CmdSwitch);
	// (the rows can change while the menu is up: what it needs is taken now)
	const auto id = t.Id;
	const auto location = t.Location;
	AppendCopyItems(menu.m_hMenu);
	auto cmd = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd);
	if (HandleCopyCommand(cmd))
		return true;
	switch (cmd) {
		case CmdSwitch: SwitchTo(id); break;
		case CmdFreeze: Apply(id, Action::Freeze); break;
		case CmdThaw: Apply(id, Action::Thaw); break;
		case CmdSuspend: Apply(id, Action::Suspend); break;
		case CmdResume: Apply(id, Action::Resume); break;
		case CmdCopy:
			if (OpenClipboard()) {
				::EmptyClipboard();
				auto& text = location;
				auto size = (text.size() + 1) * sizeof(wchar_t);
				if (auto hData = ::GlobalAlloc(GMEM_MOVEABLE, size)) {
					memcpy(::GlobalLock(hData), text.c_str(), size);
					::GlobalUnlock(hData);
					if (!::SetClipboardData(CF_UNICODETEXT, hData))
						::GlobalFree(hData);
				}
				::CloseClipboard();
			}
			break;
	}
	return true;
}

DebugEngine::Task<void> CUserThreadsView::Apply(ULONG threadId, Action action) {
	auto alive = m_Lifetime.Watch();
	auto& dbg = DebugManager::Debugger();
	HRESULT hr = E_FAIL;
	switch (action) {
		case Action::Freeze: hr = co_await dbg.SetThreadFrozen(threadId, true); break;
		case Action::Thaw: hr = co_await dbg.SetThreadFrozen(threadId, false); break;
		case Action::Suspend: hr = co_await dbg.SuspendThread(threadId, true); break;
		case Action::Resume: hr = co_await dbg.SuspendThread(threadId, false); break;
	}
	if (!*alive)
		co_return;
	if (FAILED(hr))
		::MessageBeep(MB_ICONWARNING);
	// (the engine does not tell of it)
	RequestRefresh();
}

void CUserThreadsView::OnThreadCreated(const DebugEngine::ThreadCreateData& data) {
	// a list that is not showing, or is not up to date yet, is brought up to date as a whole
	if (IsDirty() || !IsWindowVisible()) {
		MarkDirty();
		return;
	}

	DebugEngine::ThreadInfo ti;
	ti.Teb = data.Teb;
	ti.Handle = data.Handle;
	ti.ThreadId = data.ThreadId;
	ti.Pid = data.Pid;
	ti.Id = data.Id;

	m_Items.push_back(ti);
	UpdateList(m_Items.size());
}

void CUserThreadsView::OnThreadExited(const DebugEngine::ExitThreadData& data) {
	if (IsDirty() || !IsWindowVisible()) {
		MarkDirty();
		return;
	}

	auto it = std::find_if(m_Items.begin(), m_Items.end(), [&](auto& ti) { return ti.ThreadId == data.ThreadId; });
	if (it != m_Items.end()) {
		m_Items.erase(it);
		UpdateList(m_Items.size());
	}
}

void CUserThreadsView::OnContextChanged() {
	RequestRefresh();
}

void CUserThreadsView::OnTargetEnded() {
	m_Items.clear();
	UpdateList(0);
	MarkDirty();
}

bool CUserThreadsView::CanRefresh() const {
	auto& session = DebugManager::Session();
	return session.IsBroken() && (session.Target() & DebugEngine::TargetType::User) == DebugEngine::TargetType::User;
}

DebugEngine::Task<void> CUserThreadsView::Refresh() {
	auto alive = m_Lifetime.Watch();

	std::vector<DebugEngine::ProcessInfo> processes;
	std::vector<DebugEngine::ThreadInfo> threads;
	co_await DebugManager::Debugger().EnumProcesses(processes, threads);
	if (!*alive)
		co_return;

	m_Items.swap(threads);
	UpdateList(m_Items.size());
	RefreshDone();
}

DebugEngine::Task<void> CUserThreadsView::SwitchTo(ULONG threadId) {
	// (the panes that are about the current thread hear of it from the engine)
	co_await DebugManager::Debugger().SetCurrentThread(threadId);
}
