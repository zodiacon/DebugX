#pragma once

#include <map>
#include "resource.h"
#include "VirtualListView.h"
#include "DialogHelper.h"

// Attach to Process: this machine's processes (or a process server's), filtered by what is typed above the list -- in
// the name, the user, the PID (decimal or hex) or the path; Escape empties the filter, Down goes to the list. Resizable.
class CProcessSelectDlg :
	public CDialogImpl<CProcessSelectDlg>,
	public CDialogResize<CProcessSelectDlg>,
	public CVirtualListView<CProcessSelectDlg>,
	public CDialogHelper<CProcessSelectDlg>,
	public CCustomDraw<CProcessSelectDlg> {
public:
	friend struct CVirtualListView<CProcessSelectDlg>;

	CProcessSelectDlg() : m_Filter(this, 1) {}

	struct ProcessInfo {
		CString Name;
		CString Path;
		CString User;		// DOMAIN\name; empty if it cannot be known (another user's process, not elevated)
		DWORD Id;
		DWORD Session;
		int Image;
	};

	enum { IDD = IDD_PROCSELECT };

	int GetSelectedProcess(CString& name) const;
	// a process server's processes (by name only), instead of this machine's
	void SetRemoteProcesses(CString server, std::vector<std::pair<DWORD, CString>> processes) {
		m_Server = server;
		m_RemoteProcesses = std::move(processes);
	}

	BEGIN_MSG_MAP(CProcessSelectDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
		COMMAND_ID_HANDLER(IDC_REFRESH, OnRefresh)
		COMMAND_HANDLER(IDC_PROCFILTER, EN_CHANGE, OnFilterChanged)
		NOTIFY_CODE_HANDLER(LVN_GETDISPINFO, OnGetDispInfo)
		NOTIFY_CODE_HANDLER(LVN_ITEMCHANGED, OnItemChanged)
		NOTIFY_CODE_HANDLER(NM_DBLCLK, OnDblClickItem)
		CHAIN_MSG_MAP(CVirtualListView<CProcessSelectDlg>)
		CHAIN_MSG_MAP(CCustomDraw<CProcessSelectDlg>)
		CHAIN_MSG_MAP(CDialogResize<CProcessSelectDlg>)
	ALT_MSG_MAP(1)
		MESSAGE_HANDLER(WM_KEYDOWN, OnFilterKeyDown)
	END_MSG_MAP()

	BEGIN_DLGRESIZE_MAP(CProcessSelectDlg)
		DLGRESIZE_CONTROL(IDC_PROCFILTER, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_PROCLIST, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_REFRESH, DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDOK, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

protected:
	void DoSort(const SortInfo* si);

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnItemChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnGetDispInfo(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnDblClickItem(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnRefresh(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD, WORD, HWND, BOOL&);
	LRESULT OnFilterKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& handled);

	void InitProcessList();
	void EnumProcesses();
	// the processes the filter lets through, sorted as the list is, into the list (the selected one stays selected, if
	// it is still there)
	void ApplyFilter();
	void Select(int index);
	// DOMAIN\user of a process (by its token's SID, looked up once per SID)
	CString UserOf(HANDLE hProcess);
	static bool CompareItems(const ProcessInfo& p1, const ProcessInfo& p2, int col, bool asc);

private:
	std::vector<ProcessInfo> m_All;		// every process
	std::vector<ProcessInfo> m_Items;	// the ones in the list
	std::map<std::wstring, CString> m_Users;	// by SID string
	int m_SelectedPid = -1;
	CString m_Name;
	CListViewCtrl m_List;
	CContainedWindowT<CEdit> m_Filter;
	CImageList m_Images;
	bool m_Updating{ false };
	CString m_Server;		// empty: this machine
	std::vector<std::pair<DWORD, CString>> m_RemoteProcesses;
};
