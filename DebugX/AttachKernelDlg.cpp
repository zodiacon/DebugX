#include "stdafx.h"
#include "AttachKernelDlg.h"

LRESULT CAttachKernelDlg::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
	AdjustOKCancelButtons(IDI_OK, IDI_DELETE);
	SetDlgItemText(IDC_KERNELCONN, m_Connection);
	CenterWindow(GetParent());

	// the text is all selected, ready to be typed over
	GotoDlgCtrl(GetDlgItem(IDC_KERNELCONN));
	return FALSE;
}

LRESULT CAttachKernelDlg::OnCloseCmd(WORD, WORD wID, HWND, BOOL&) {
	if (wID == IDOK) {
		GetDlgItemText(IDC_KERNELCONN, m_Connection);
		m_Connection.Trim();
	}
	EndDialog(wID);
	return 0;
}

LRESULT CAttachKernelDlg::OnConnectionChanged(WORD, WORD, HWND, BOOL&) {
	CString text;
	GetDlgItemText(IDC_KERNELCONN, text);
	text.Trim();
	GetDlgItem(IDOK).EnableWindow(!text.IsEmpty());
	return 0;
}
