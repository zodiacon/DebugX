#include "stdafx.h"
#include "UserThreadsView.h"
#include "DebugManager.h"
#include "SortHelper.h"

CString CUserThreadsView::GetColumnText(HWND hWnd, int row, int column) const {
	auto& t = m_Items[row];
	CString text;

	switch (column) {
		case 0: text.Format(L"%u", t.Id); break;
		case 1: text.Format(L"%u (0x%X)", t.ThreadId, t.ThreadId); break;
		case 2: text.Format(L"%u (0x%X)", t.Pid, t.Pid); break;
		case 3: text.Format(L"0x%llX", t.Teb); break;
	}

	return text;
}

int CUserThreadsView::GetRowImage(HWND, int row, int col) const {
	return 0;
}

void CUserThreadsView::DoSort(const SortInfo* si) {
	if (si == nullptr)
		return;

	std::sort(m_Items.begin(), m_Items.end(), [&](const auto& t1, const auto& t2) {
		switch (si->SortColumn) {
			case 0: return SortHelper::Sort(t1.Id, t2.Id, si->SortAscending);
			case 1: return SortHelper::Sort(t1.ThreadId, t2.ThreadId, si->SortAscending);
			case 2: return SortHelper::Sort(t1.Pid, t2.Pid, si->SortAscending);
			case 3: return SortHelper::Sort(t1.Teb, t2.Teb, si->SortAscending);
		}
		return false;
		});
}

HWND CUserThreadsView::Create(HWND hParent) {
	return __super::Create(hParent, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
}

std::wstring CUserThreadsView::SaveState() const {
	return PaneState::SaveColumnWidths(m_List);
}

void CUserThreadsView::LoadState(PCWSTR state) {
	PaneState::LoadColumnWidths(m_List, state);
}

void CUserThreadsView::OnFinalMessage(HWND) {
	delete this;
}

void CUserThreadsView::UpdateList() {
	DoSort(GetSortInfo(m_List));
	m_List.SetItemCountEx(static_cast<int>(m_Items.size()), LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
	m_List.RedrawItems(m_List.GetTopIndex(), m_List.GetTopIndex() + m_List.GetCountPerPage());
}

LRESULT CUserThreadsView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_List.Create(*this, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS
		| LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS);
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);

	m_List.InsertColumn(0, L"Index", LVCFMT_LEFT, 80);
	m_List.InsertColumn(1, L"TID", LVCFMT_RIGHT, 100);
	m_List.InsertColumn(2, L"PID", LVCFMT_RIGHT, 110);
	m_List.InsertColumn(3, L"TEB", LVCFMT_RIGHT, 100);

	CImageList images;
	images.Create(16, 16, ILC_COLOR32, 2, 0);
	images.AddIcon(AtlLoadIconImage(IDI_THREADS, 0, 16, 16));
	m_List.SetImageList(images, LVSIL_SMALL);

	InitPane();

	return 0;
}

LRESULT CUserThreadsView::OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
	ExitPane();
	handled = FALSE;
	return 0;
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
	UpdateList();
}

void CUserThreadsView::OnThreadExited(const DebugEngine::ExitThreadData& data) {
	if (IsDirty() || !IsWindowVisible()) {
		MarkDirty();
		return;
	}

	auto it = std::find_if(m_Items.begin(), m_Items.end(), [&](auto& ti) { return ti.ThreadId == data.ThreadId; });
	if (it != m_Items.end()) {
		m_Items.erase(it);
		UpdateList();
	}
}

void CUserThreadsView::OnTargetEnded() {
	m_Items.clear();
	UpdateList();
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
	co_await DebugManager::DebuggerX().EnumProcesses(processes, threads);
	if (!*alive)
		co_return;

	m_Items.swap(threads);
	UpdateList();
	RefreshDone();
}
