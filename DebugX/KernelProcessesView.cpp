#include "stdafx.h"
#include "KernelProcessesView.h"
#include "DebugManager.h"
#include "DebugX.Kernel.h"
#include "SortHelper.h"

HWND CKernelProcessesView::Create(HWND hParent) {
	return __super::Create(hParent, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
}

void CKernelProcessesView::OnFinalMessage(HWND) {
	delete this;
}

std::wstring CKernelProcessesView::SaveState() const {
	return PaneState::SaveColumnWidths(m_List);
}

void CKernelProcessesView::LoadState(PCWSTR state) {
	PaneState::LoadColumnWidths(m_List, state);
}

void CKernelProcessesView::OnTargetEnded() {
	m_Items.clear();
	m_List.SetItemCount(0);
	MarkDirty();
}

bool CKernelProcessesView::CanRefresh() const {
	auto& session = DebugManager::Session();
	return (session.Target() & DebugEngine::TargetType::Kernel) == DebugEngine::TargetType::Kernel;
}

DebugEngine::Task<void> CKernelProcessesView::Refresh() {
	using namespace DebugX::Analysis;

	auto alive = m_Lifetime.Watch();

	auto enumeration = std::make_shared<Enumeration>();
	enumeration->Processes.reserve(256);
	m_Enumeration = enumeration;
	m_Items.clear();
	m_List.SetItemCount(1);
	m_List.EnableWindow(FALSE);
	SetTimer(1, 1000);

	co_await DebugManager::DebuggerX().RunAsync([enumeration](IDebugClient* client) {
		KernelProcessEnumerator enumerator(client);
		KernelProcessAnalyzer* p;
		while (p = enumerator.GetNextProcess()) {
			KernelProcess kp;
			kp.Pid = p->GetProcessId();
			kp.Address = p->GetProcessAddress();
			kp.ImageName = p->GetImageName();
			kp.ThreadCount = p->GetThreadCount();
			kp.CreateTime = p->GetCreateTime();
			kp.ExitTime = p->GetExitTime();
			kp.SessionId = p->GetSessionId();
			kp.DirBase = p->GetDirectoryTableBase();
			kp.Peb = p->GetPeb();
			//kp.KernelTime = p->GetKernelTime();
			//kp.UserTime = p->GetUserTime();
			enumeration->Processes.push_back(std::move(kp));
			enumeration->Count = enumeration->Processes.size();
		}
		return S_OK;
		});
	if (!*alive)
		co_return;

	KillTimer(1);
	m_Items.Set(std::move(enumeration->Processes));
	m_Enumeration = nullptr;
	m_List.SetItemCount(static_cast<int>(m_Items.size()));
	DoSort(GetSortInfo());
	m_List.EnableWindow(TRUE);
	RefreshDone();
}

void CKernelProcessesView::DoSort(const SortInfo* si) {
	if (si == nullptr)
		return;

	auto asc = si->SortAscending;
	m_Items.Sort([&](const auto& p1, const auto& p2) {
		switch (si->SortColumn) {
			case 0: return SortHelper::Sort(p1.ImageName, p2.ImageName, asc);
			case 1: return SortHelper::Sort(p1.Address, p2.Address, asc);
			case 2: return SortHelper::Sort(p1.Pid, p2.Pid, asc);
			case 3: return SortHelper::Sort(p1.ThreadCount, p2.ThreadCount, asc);
			case 4: return SortHelper::Sort(p1.SessionId, p2.SessionId, asc);
			case 5: return SortHelper::Sort(p1.CreateTime.QuadPart, p2.CreateTime.QuadPart, asc);
			case 6: return SortHelper::Sort(p1.DirBase, p2.DirBase, asc);
			case 7: return SortHelper::Sort(p1.Peb, p2.Peb, asc);
			case 8: return SortHelper::Sort(p1.ExitTime.QuadPart, p2.ExitTime.QuadPart, asc);
			default: return false;
		}
		});
}

LRESULT CKernelProcessesView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_List.Create(*this, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS
		| LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS);
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);

	m_List.InsertColumn(0, L"Image Name", LVCFMT_LEFT, 200);
	m_List.InsertColumn(1, L"EPROCESS", LVCFMT_RIGHT, 140);
	m_List.InsertColumn(2, L"PID", LVCFMT_RIGHT, 110);
	m_List.InsertColumn(3, L"Threads", LVCFMT_RIGHT, 60);
	m_List.InsertColumn(4, L"Session", LVCFMT_RIGHT, 60);
	m_List.InsertColumn(5, L"Create Time", LVCFMT_RIGHT, 120);
	m_List.InsertColumn(6, L"Dir Base", LVCFMT_RIGHT, 120);
	m_List.InsertColumn(7, L"PEB", LVCFMT_RIGHT, 100);
	m_List.InsertColumn(8, L"Exit Time", LVCFMT_RIGHT, 120);
	//m_List.InsertColumn(6, L"Kernel Time", LVCFMT_RIGHT, 120);
	//m_List.InsertColumn(7, L"User Time", LVCFMT_RIGHT, 120);

	InitPane();

	return 0;
}

LRESULT CKernelProcessesView::OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
	ExitPane();
	handled = FALSE;
	return 0;
}

LRESULT CKernelProcessesView::OnTimer(UINT, WPARAM id, LPARAM, BOOL&) {
	if (id == 1 && m_Enumeration)
		m_List.RedrawItems(0, 0);

	return 0;
}

LRESULT CKernelProcessesView::OnViewRefresh(WORD, WORD, HWND, BOOL&) {
	RequestRefresh();

	return 0;
}

LRESULT CKernelProcessesView::OnListGetDispInfo(int, LPNMHDR hdr, BOOL&) {
	auto lv = (NMLVDISPINFO*)hdr;
	auto& item = lv->item;
	if (m_Enumeration) {
		if (item.iSubItem == 0)
			::StringCchPrintf(item.pszText, item.cchTextMax, L"Refreshing processes... %u", static_cast<ULONG>(m_Enumeration->Count));
		return 0;
	}
	auto& data = m_Items[item.iItem];

	static std::function<void(KernelProcess&, LVITEM&)> functions[] = {
		[](auto& data, auto& item) { item.pszText = (PWSTR)(PCWSTR)data.ImageName;	},
		[](auto& data, auto& item) { ::StringCchPrintf(item.pszText, item.cchTextMax, L"0x%llX", data.Address); },
		[](auto& data, auto& item) { ::StringCchPrintf(item.pszText, item.cchTextMax, L"%d (0x%X)", data.Pid, data.Pid); },
		[](auto& data, auto& item) { ::StringCchPrintf(item.pszText, item.cchTextMax, L"%d", data.ThreadCount);	},
		[](auto& data, auto& item) { ::StringCchPrintf(item.pszText, item.cchTextMax, L"%u", data.SessionId);	},
		[](auto& data, auto& item) { ::StringCchCopy(item.pszText, item.cchTextMax, (PCWSTR)CTime(*(FILETIME*)&data.CreateTime).Format(L"%D %X")); },
		[](auto& data, auto& item) { ::StringCchPrintf(item.pszText, item.cchTextMax, L"0x%llX", data.DirBase);	},
		[](auto& data, auto& item) { ::StringCchPrintf(item.pszText, item.cchTextMax, L"0x%llX", data.Peb);	},
		[](auto& data, auto& item) { 
			if (data.ExitTime.QuadPart)
				::StringCchCopy(item.pszText, item.cchTextMax, (PCWSTR)CTime(*(FILETIME*)&data.ExitTime).Format(L"%D %X"));
			else
				::StringCchCopy(item.pszText, item.cchTextMax, L"(still running)");
	},
		//[](auto& data, auto& item) { ::StringCchCopy(item.pszText, item.cchTextMax, CTimeSpan(data.KernelTime / 10000000LL).Format(L"%D %H:%M:%S")); },
		//[](auto& data, auto& item) { ::StringCchCopy(item.pszText, item.cchTextMax, CTimeSpan(data.UserTime / 10000000LL).Format(L"%D %H:%M:%S")); },

	};

	if (item.mask & LVIF_TEXT) {
		if (item.iSubItem < _countof(functions)) {
			auto f = functions[item.iSubItem];
			if(f)
				f(data, item);
		}
	}

	return 0;
}

