#include "pch.h"
#include "ProcessSelectDlg.h"
#include <string>
#include <TlHelp32.h>
#include <sddl.h>
#include <algorithm>
#include "SortHelper.h"

int CProcessSelectDlg::GetSelectedProcess(CString& name) const {
	name = m_Name;
	return m_SelectedPid;
}

void CProcessSelectDlg::DoSort(const SortInfo* si) {
	if (!si)
		return;

	std::sort(m_Items.begin(), m_Items.end(), [si](const auto& p1, const auto& p2) {
		return CompareItems(p1, p2, si->SortColumn, si->SortAscending);
		});
	// (a column's header was clicked: the list's selection is by index -- put it on the same process, where it is now)
	if (!m_Updating && m_List && m_SelectedPid >= 0) {
		m_Updating = true;
		m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
		m_Updating = false;
		auto it = std::find_if(m_Items.begin(), m_Items.end(), [&](auto& p) { return static_cast<int>(p.Id) == m_SelectedPid; });
		if (it != m_Items.end())
			Select(static_cast<int>(it - m_Items.begin()));
	}
}

LRESULT CProcessSelectDlg::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
	AdjustOKCancelButtons(IDI_OK, IDI_DELETE);
	// (a size grip; the initial size is the smallest)
	DlgResize_Init(true, true);

	m_List.Attach(GetDlgItem(IDC_PROCLIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
	m_List.InsertColumn(0, L"Name", LVCFMT_LEFT, 170);
	m_List.InsertColumn(1, L"PID", LVCFMT_RIGHT, 110);
	m_List.InsertColumn(2, L"Session", LVCFMT_CENTER, 60);
	m_List.InsertColumn(3, L"User", LVCFMT_LEFT, 220);

	m_Images.Create(16, 16, ILC_COLOR32, 32, 16);
	m_List.SetImageList(m_Images, LVSIL_SMALL);
	m_Filter.SubclassWindow(GetDlgItem(IDC_PROCFILTER));
	m_Filter.SetCueBannerText(L"Name, user, PID or path");

	((CButton)GetDlgItem(IDC_REFRESH)).SetIcon(AtlLoadIconImage(IDI_REFRESH, 0, 24, 24));
	if (!m_Server.IsEmpty()) {
		// (the list was asked for before: it is not asked for again from here)
		GetDlgItem(IDC_REFRESH).EnableWindow(FALSE);
		SetWindowText(L"Attach to a Process on " + m_Server);
	}

	InitProcessList();
	CenterWindow(GetParent());

	// (the focus in the filter box)
	return TRUE;
}

LRESULT CProcessSelectDlg::OnCloseCmd(WORD, WORD wID, HWND, BOOL&) {
	// Escape in the filter box empties it first
	if (wID == IDCANCEL && ::GetFocus() == m_Filter && m_Filter.GetWindowTextLength() > 0) {
		m_Filter.SetWindowText(L"");
		return 0;
	}
	if (wID == IDOK && m_SelectedPid < 0)
		return 0;
	EndDialog(wID);
	return 0;
}

LRESULT CProcessSelectDlg::OnItemChanged(int, LPNMHDR, BOOL&) {
	if (m_Updating)
		return 0;
	auto selected = m_List.GetSelectedIndex();
	GetDlgItem(IDOK).EnableWindow(selected >= 0);
	if (selected >= 0 && selected < static_cast<int>(m_Items.size())) {
		m_SelectedPid = m_Items[selected].Id;
		m_Name = m_Items[selected].Name;
	}
	else
		m_SelectedPid = -1;
	return 0;
}

LRESULT CProcessSelectDlg::OnGetDispInfo(int, LPNMHDR hdr, BOOL&) {
	auto di = reinterpret_cast<NMLVDISPINFO*>(hdr);
	auto& item = di->item;
	auto row = item.iItem;
	if (row < 0 || row >= static_cast<int>(m_Items.size()))
		return 0;
	auto col = item.iSubItem;
	auto& data = m_Items[row];

	if (item.mask & LVIF_TEXT) {
		switch (col) {
		case 0:		// name
			item.pszText = (PWSTR)(PCWSTR)data.Name;
			break;

		case 1:		// ID
			::StringCchPrintf(item.pszText, item.cchTextMax, L"%d (0x%X)", data.Id, data.Id);
			break;

		case 2:		// session
			::StringCchPrintf(item.pszText, item.cchTextMax, L"%d", data.Session);
			break;

		case 3:		// user
			item.pszText = (PWSTR)(PCWSTR)data.User;
			break;
		}
	}
	if (item.mask & LVIF_IMAGE)
		item.iImage = data.Image;

	return 0;
}

LRESULT CProcessSelectDlg::OnDblClickItem(int, LPNMHDR, BOOL&) {
	auto selected = m_List.GetSelectedIndex();
	if (selected >= 0)
		EndDialog(IDOK);
	return 0;
}

LRESULT CProcessSelectDlg::OnRefresh(WORD, WORD wID, HWND, BOOL&) {
	InitProcessList();

	return 0;
}

LRESULT CProcessSelectDlg::OnFilterChanged(WORD, WORD, HWND, BOOL&) {
	if (m_List)
		ApplyFilter();
	return 0;
}

LRESULT CProcessSelectDlg::OnFilterKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& handled) {
	// Down: to the list (its first process, if none is selected)
	if (wParam == VK_DOWN && !m_Items.empty()) {
		if (m_SelectedPid < 0)
			Select(0);
		m_List.SetFocus();
		return 0;
	}
	handled = FALSE;
	return 0;
}

void CProcessSelectDlg::Select(int index) {
	m_List.SetItemState(index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	m_List.EnsureVisible(index, FALSE);
	m_SelectedPid = m_Items[index].Id;
	m_Name = m_Items[index].Name;
	GetDlgItem(IDOK).EnableWindow(TRUE);
}

void CProcessSelectDlg::InitProcessList() {
	EnumProcesses();
	ApplyFilter();
}

void CProcessSelectDlg::ApplyFilter() {
	CString filter;
	m_Filter.GetWindowText(filter);
	filter.Trim();
	filter.MakeLower();

	m_Items.clear();
	for (auto& p : m_All) {
		if (!filter.IsEmpty()) {
			CString pid;
			pid.Format(L"%u 0x%x", p.Id, p.Id);
			CString text = p.Name + L"\n" + p.User + L"\n" + p.Path + L"\n" + pid;
			text.MakeLower();
			if (text.Find(filter) < 0)
				continue;
		}
		m_Items.push_back(p);
	}

	// (the list's selection is by index: it is put on the same process again, wherever that is now)
	m_Updating = true;
	DoSort(GetSortInfo());
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	m_List.SetItemCountEx(static_cast<int>(m_Items.size()), LVSICF_NOSCROLL);
	m_Updating = false;
	auto it = std::find_if(m_Items.begin(), m_Items.end(), [&](auto& p) { return static_cast<int>(p.Id) == m_SelectedPid; });
	if (it != m_Items.end())
		Select(static_cast<int>(it - m_Items.begin()));
	else if (m_Items.size() == 1 && !filter.IsEmpty())
		Select(0);		// (the one the filter leaves: Enter attaches to it)
	else {
		m_SelectedPid = -1;
		GetDlgItem(IDOK).EnableWindow(FALSE);
	}
	m_List.Invalidate();
}

CString CProcessSelectDlg::UserOf(HANDLE hProcess) {
	HANDLE hToken;
	if (!::OpenProcessToken(hProcess, TOKEN_QUERY, &hToken))
		return {};
	BYTE buffer[SECURITY_MAX_SID_SIZE + sizeof(TOKEN_USER)];
	DWORD size = 0;
	CString user;
	if (::GetTokenInformation(hToken, TokenUser, buffer, sizeof(buffer), &size)) {
		auto sid = reinterpret_cast<TOKEN_USER*>(buffer)->User.Sid;
		PWSTR sidText = nullptr;
		std::wstring key;
		if (::ConvertSidToStringSid(sid, &sidText)) {
			key = sidText;
			::LocalFree(sidText);
		}
		if (auto it = m_Users.find(key); !key.empty() && it != m_Users.end())
			user = it->second;
		else {
			WCHAR name[256], domain[256];
			DWORD nameSize = _countof(name), domainSize = _countof(domain);
			SID_NAME_USE use;
			if (::LookupAccountSid(nullptr, sid, name, &nameSize, domain, &domainSize, &use))
				user = domainSize > 0 && domain[0] ? CString(domain) + L"\\" + name : CString(name);
			else
				user = key.c_str();
			if (!key.empty())
				m_Users[key] = user;
		}
	}
	::CloseHandle(hToken);
	return user;
}

void CProcessSelectDlg::EnumProcesses() {
	m_All.clear();
	if (!m_Server.IsEmpty()) {
		for (auto& [pid, name] : m_RemoteProcesses)
			m_All.push_back({ name, CString(), CString(), pid, 0, 0 });
		return;
	}
	// (the icons again, from the start)
	m_Images.RemoveAll();
	m_Images.AddIcon(AtlLoadSysIcon(IDI_APPLICATION));

	auto hSnapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnapshot == INVALID_HANDLE_VALUE)
		return;

	PROCESSENTRY32 pe;
	pe.dwSize = sizeof(pe);
	::Process32First(hSnapshot, &pe);
	WCHAR imagePath[MAX_PATH];
	// (the first is the idle process: nothing to attach to)
	while (::Process32Next(hSnapshot, &pe)) {
		ProcessInfo pi;
		pi.Path = pe.szExeFile;
		if (auto hProcess = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID)) {
			DWORD size = _countof(imagePath);
			if (::QueryFullProcessImageName(hProcess, 0, imagePath, &size))
				pi.Path = imagePath;
			pi.User = UserOf(hProcess);
			::CloseHandle(hProcess);
		}
		HICON hIcon;
		pi.Image = 0;
		if (::ExtractIconEx(pi.Path, 0, nullptr, &hIcon, 1) == 1 && hIcon) {
			pi.Image = m_Images.AddIcon(hIcon);
			::DestroyIcon(hIcon);
		}
		pi.Name = pe.szExeFile;
		pi.Id = pe.th32ProcessID;
		pi.Session = 0;
		::ProcessIdToSessionId(pi.Id, &pi.Session);
		m_All.push_back(std::move(pi));
	}
	::CloseHandle(hSnapshot);
}

bool CProcessSelectDlg::CompareItems(const ProcessInfo& p1, const ProcessInfo& p2, int col, bool asc) {
	switch (col) {
	case 0:	return SortHelper::Sort(p1.Name, p2.Name, asc);
	case 1: return SortHelper::Sort(p1.Id, p2.Id, asc);
	case 2: return SortHelper::Sort(p1.Session, p2.Session, asc);
	case 3: return SortHelper::Sort(p1.User, p2.User, asc);
	}
	ATLASSERT(false);
	return false;
}
