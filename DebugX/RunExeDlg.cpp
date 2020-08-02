#include "stdafx.h"
#include "RunExeDlg.h"
#include "DialogHelper.h"

LRESULT CRunExeDlg::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
	DialogHelper::AdjustOKCancelButtons(this);
	DialogHelper::AddIconToButton(this, IDC_BROWSE, IDI_OPEN);
	DialogHelper::AddIconToButton(this, IDC_BROWSEDIR, IDI_OPEN);

	::SHAutoComplete(GetDlgItem(IDC_EXEPATH), SHACF_FILESYSTEM);
	::SHAutoComplete(GetDlgItem(IDC_INITIALDIR), SHACF_FILESYS_DIRS);

	CenterWindow();

	return 0;
}

LRESULT CRunExeDlg::OnCloseCmd(WORD, WORD wID, HWND, BOOL&) {
	if (wID == IDOK) {
		GetDlgItem(IDC_EXEPATH).GetWindowText(m_ExePath);
		GetDlgItem(IDC_ARGS).GetWindowText(m_Arguments);
		GetDlgItem(IDC_INITIALDIR).GetWindowText(m_WorkingDir);
		m_InitialBreakpoint = IsDlgButtonChecked(IDC_INITIALBREAK) == BST_CHECKED;
		m_NonInvasiveAttach = IsDlgButtonChecked(IDC_NONINVASIVE) == BST_CHECKED;
	}

	EndDialog(wID);
	return 0;
}

LRESULT CRunExeDlg::OnExePathChanged(WORD, WORD wID, HWND, BOOL&) {
	GetDlgItem(IDOK).EnableWindow(GetDlgItem(IDC_EXEPATH).GetWindowTextLength() > 0);

	return 0;
}

LRESULT CRunExeDlg::OnBrowse(WORD, WORD wID, HWND, BOOL&) {
	CFileDialog dlg(TRUE, L"exe", nullptr,
		OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ENABLESIZING,
		L"Executable Files (*.exe)\0*.exe\0All Files\0*.*\0", *this);
	if (dlg.DoModal() == IDOK) {
		SetDlgItemText(IDC_EXEPATH, dlg.m_szFileName);
	}
	return 0;
}

LRESULT CRunExeDlg::OnBrowseDir(WORD, WORD wID, HWND, BOOL&) {
	CFolderDialog dlg(*this, L"Select working directory");
	if (dlg.DoModal() == IDOK) {
		SetDlgItemText(IDC_INITIALDIR, dlg.m_szFolderPath);
	}
	return 0;
}

