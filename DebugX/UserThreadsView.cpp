#include "stdafx.h"
#include "UserThreadsView.h"

LRESULT CUserThreadsView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_List.Create(*this, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS
		| LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS);
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);

	m_List.InsertColumn(0, L"Image Name", LVCFMT_LEFT, 200);
	m_List.InsertColumn(1, L"EPROCESS", LVCFMT_RIGHT, 140);
	m_List.InsertColumn(2, L"PID", LVCFMT_RIGHT, 110);
	m_List.InsertColumn(3, L"Threads", LVCFMT_RIGHT, 60);

	return 0;
}

LRESULT CUserThreadsView::OnListGetDispInfo(int, LPNMHDR, BOOL&) {
	return LRESULT();
}
