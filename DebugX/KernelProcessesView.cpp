#include "stdafx.h"
#include "KernelProcessesView.h"
#include "DebugManager.h"
#include "DebugX.Kernel.h"
#include "SortHelper.h"

HICON CKernelProcessesView::GetIcon() const {
	return AtlLoadIconImage(IDI_PROCESSES, 64, 16, 16);
}

CString CKernelProcessesView::GetHeader() const {
	return L"Processes";
}

HWND CKernelProcessesView::Create(HWND hParent) {
	return __super::Create(hParent, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
}

void CKernelProcessesView::Refresh() {
	m_List.SetItemCount(1);
	m_Loading = true;
	m_List.EnableWindow(FALSE);
	m_Items.Clear();
	m_Items.Reserve(256);
	SetTimer(1, 1000, nullptr);

	DebugManager::Debugger().RunAsync([&](IDebugClient* client) {
		using namespace DebugX::Analysis;
		KernelProcessEnumerator enumerator(client);
		KernelProcessAnalyzer* p;
		while (p = enumerator.GetNextProcess()) {
			KernelProcess kp;
			kp.Pid = p->GetProcessId();
			kp.Address = p->GetProcessAddress();
			kp.ImageName = p->GetImageName();
			kp.ThreadCount = p->GetThreadCount();
			kp.CreateTime = p->GetCreateTime();
			kp.SessionId = p->GetSessionId();
			kp.DirBase = p->GetDirectoryTableBase();
			kp.Peb = p->GetPeb();
			//kp.KernelTime = p->GetKernelTime();
			//kp.UserTime = p->GetUserTime();
			m_Items.Add(std::move(kp));
		}

		});

	KillTimer(1);
	m_List.SetItemCount(static_cast<int>(m_Items.FilteredSize()));
	m_Loading = false;
	DoSort(GetSortInfo());
	m_List.EnableWindow(TRUE);
}

void CKernelProcessesView::DoSort(const SortInfo* si) {
	if (si == nullptr)
		return;

	auto asc = si->SortAscending;
	m_Items.Sort([&](const auto& p1, const auto& p2) {
		switch (si->SortColumn) {
			case 0: return SortHelper::SortStrings(p1.ImageName, p2.ImageName, asc);
			case 1: return SortHelper::SortNumbers(p1.Address, p2.Address, asc);
			case 2: return SortHelper::SortNumbers(p1.Pid, p2.Pid, asc);
			case 3: return SortHelper::SortNumbers(p1.ThreadCount, p2.ThreadCount, asc);
			case 4: return SortHelper::SortNumbers(p1.SessionId, p2.SessionId, asc);
			case 5: return SortHelper::SortNumbers(p1.CreateTime.QuadPart, p2.CreateTime.QuadPart, asc);
			case 6: return SortHelper::SortNumbers(p1.DirBase, p2.DirBase, asc);
			case 7: return SortHelper::SortNumbers(p1.Peb, p2.Peb, asc);
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
	//m_List.InsertColumn(6, L"Kernel Time", LVCFMT_RIGHT, 120);
	//m_List.InsertColumn(7, L"User Time", LVCFMT_RIGHT, 120);

	PostMessage(WM_COMMAND, ID_VIEW_REFRESH);

	return 0;
}

LRESULT CKernelProcessesView::OnTimer(UINT, WPARAM id, LPARAM, BOOL&) {
	if (id == 1 && m_Loading)
		m_List.RedrawItems(0, 0);

	return 0;
}

LRESULT CKernelProcessesView::OnViewRefresh(WORD, WORD, HWND, BOOL&) {
	CWaitCursor wait;
	Refresh();

	return 0;
}

LRESULT CKernelProcessesView::OnListGetDispInfo(int, LPNMHDR hdr, BOOL&) {
	auto lv = (NMLVDISPINFO*)hdr;
	auto& item = lv->item;
	if (m_Loading) {
		if (item.iSubItem == 0)
			::StringCchPrintf(item.pszText, item.cchTextMax,L"Refreshing processes... %u", m_Items.Size());
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

