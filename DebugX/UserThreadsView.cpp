#include "stdafx.h"
#include "UserThreadsView.h"
#include "DebugManager.h"

CString CUserThreadsView::GetColumnText(HWND hWnd, int row, int column) const {
	auto& t = m_Items[row];
	CString text;

	switch (column) {
		case 0: text.Format(L"%u", t.Id); break;
		case 1: text.Format(L"%u (0x%u)", t.ThreadId, t.ThreadId); break;
		case 2: text.Format(L"%u (0x%u)", t.Pid, t.Pid); break;
		case 3: text.Format(L"0x%llX", t.Teb); break;
	}

	return text;
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

LRESULT CUserThreadsView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_List.Create(*this, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS
		| LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS);
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);

	m_List.InsertColumn(0, L"Index", LVCFMT_LEFT, 80);
	m_List.InsertColumn(1, L"TID", LVCFMT_RIGHT, 100);
	m_List.InsertColumn(2, L"PID", LVCFMT_RIGHT, 110);
	m_List.InsertColumn(3, L"TEB", LVCFMT_RIGHT, 100);

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

	m_Items.push_back(ti);
	m_List.SetItemCountEx(static_cast<int>(m_Items.size()), LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);

	return 0;
}

LRESULT CUserThreadsView::OnStatusChanged(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto data = *reinterpret_cast<DebugEngine::ExecutionStatus*>(lParam);
	//if (data == DebugEngine::ExecutionStatus::Break)
	//	Refresh();

	return 0;
}

void CUserThreadsView::Refresh() {
	m_Items.clear();
	std::vector<DebugEngine::ProcessInfo> processes;
	DebugManager::DebuggerX().EnumProcesses(processes, m_Items);

	m_List.SetItemCountEx(static_cast<int>(m_Items.size()), LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
}

