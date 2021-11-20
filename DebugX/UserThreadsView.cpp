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

int CUserThreadsView::GetRowImage(int row) const {
	return 0;
}

void CUserThreadsView::DoSort(const SortInfo* si) {
	if (si == nullptr)
		return;

	std::sort(m_Items.begin(), m_Items.end(), [&](const auto& t1, const auto& t2) {
		switch (si->SortColumn) {
			case 0: return SortHelper::SortNumbers(t1.Id, t2.Id, si->SortAscending);
			case 1: return SortHelper::SortNumbers(t1.ThreadId, t2.ThreadId, si->SortAscending);
			case 2: return SortHelper::SortNumbers(t1.Pid, t2.Pid, si->SortAscending);
			case 3: return SortHelper::SortNumbers(t1.Teb, t2.Teb, si->SortAscending);
		}
		return false;
		});
}

HICON CUserThreadsView::GetIcon() const {
	return AtlLoadIconImage(IDI_THREADS, 0, 16, 16);
}

CString CUserThreadsView::GetHeader() const {
	return L"Threads";
}

HWND CUserThreadsView::Create(HWND hParent) {
	return __super::Create(hParent, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
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

	DebugManager::Get().RegisterWindow(*this);

	if(DebugManager::DebuggerX().GetExecutionStatusAsync() == DebugEngine::ExecutionStatus::Break)
		Refresh();

	return 0;
}

LRESULT CUserThreadsView::OnDestroy(UINT, WPARAM, LPARAM, BOOL&) {
	DebugManager::Get().UnregisterWindow(*this);
	return 0;
}

LRESULT CUserThreadsView::OnThreadCreated(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto data = reinterpret_cast<DebugEngine::ThreadCreateData*>(lParam);
	DebugEngine::ThreadInfo ti;
	ti.Teb = data->Teb;
	ti.Handle = data->Handle;
	ti.ThreadId = data->ThreadId;
	ti.Pid = data->Pid;
	ti.Id = data->Id;

	m_Items.push_back(ti);
	UpdateList();

	return 0;
}

LRESULT CUserThreadsView::OnThreadExit(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto data = reinterpret_cast<DebugEngine::ExitThreadData*>(lParam);
	auto it = std::find_if(m_Items.begin(), m_Items.end(), [&](auto& ti) { return ti.ThreadId == data->ThreadId; });
	ATLASSERT(it != m_Items.end());
	m_Items.erase(it);
	UpdateList();

	return 0;
}

LRESULT CUserThreadsView::OnStatusChanged(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto data = *reinterpret_cast<DebugEngine::ExecutionStatus*>(lParam);
	//if (data == DebugEngine::ExecutionStatus::Break)
	//	Refresh();

	return 0;
}

LRESULT CUserThreadsView::OnBreakpoint(UINT, WPARAM, LPARAM, BOOL&) {
	Refresh();
	return 0;
}

void CUserThreadsView::Refresh() {
	m_Items.clear();
	std::vector<DebugEngine::ProcessInfo> processes;
	std::vector<DebugEngine::ThreadInfo> threads;
	DebugManager::DebuggerX().EnumProcesses(processes, threads);
	m_Items.swap(threads);
	m_List.SetItemCountEx(static_cast<int>(m_Items.size()), LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
}

