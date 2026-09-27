#include "stdafx.h"
#include "UserThreadsView.h"
#include "DebugManager.h"
#include "SortHelper.h"

void CUserThreadsView::InitList() {
	m_List.InsertColumn(0, L"Index", LVCFMT_LEFT, 80);
	m_List.InsertColumn(1, L"TID", LVCFMT_RIGHT, 100);
	m_List.InsertColumn(2, L"PID", LVCFMT_RIGHT, 110);
	m_List.InsertColumn(3, L"TEB", LVCFMT_RIGHT, 100);

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
		case 2: text.Format(L"%u (0x%X)", t.Pid, t.Pid); break;
		case 3: text.Format(L"0x%llX", t.Teb); break;
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
			case 2: return SortHelper::Sort(t1.Pid, t2.Pid, si->SortAscending);
			case 3: return SortHelper::Sort(t1.Teb, t2.Teb, si->SortAscending);
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
	co_await DebugManager::DebuggerX().EnumProcesses(processes, threads);
	if (!*alive)
		co_return;

	m_Items.swap(threads);
	UpdateList(m_Items.size());
	RefreshDone();
}

DebugEngine::Task<void> CUserThreadsView::SwitchTo(ULONG threadId) {
	// (the panes that are about the current thread hear of it from the engine)
	co_await DebugManager::DebuggerX().SetCurrentThread(threadId);
}
