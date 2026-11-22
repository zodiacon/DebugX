#pragma once

#include "resource.h"
#include "DialogHelper.h"

class CRunExeDlg :
	public CDialogImpl<CRunExeDlg>,
	public CDialogHelper<CRunExeDlg> {
public:
	enum { IDD = IDD_RUNEXE };

	BEGIN_MSG_MAP(CRunExeDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
		COMMAND_HANDLER(IDC_EXEPATH, EN_CHANGE, OnExePathChanged)
		COMMAND_ID_HANDLER(IDC_BROWSE, OnBrowse)
		COMMAND_ID_HANDLER(IDC_BROWSEDIR, OnBrowseDir)
	END_MSG_MAP()

	// the program to start with (a file dropped on the window); the arguments are typed
	explicit CRunExeDlg(PCWSTR exePath = nullptr) : m_ExePath(exePath) {}

	const CString& GetExePath() const {
		return m_ExePath;
	}

	const CString& GetArguments() const {
		return m_Arguments;
	}

	const CString& GetWorkingDir() const {
		return m_WorkingDir;
	}

	bool IsInitialBreak() const {
		return m_InitialBreakpoint;
	}

	bool IsNonInvasiveAttach() const {
		return m_NonInvasiveAttach;
	}

	// Handler prototypes (uncomment arguments if needed):
	//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnExePathChanged(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnBrowse(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnBrowseDir(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

private:
	CString m_ExePath, m_Arguments, m_WorkingDir;
	bool m_InitialBreakpoint;
	bool m_NonInvasiveAttach;
};

