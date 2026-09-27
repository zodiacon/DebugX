#include "stdafx.h"
#include "AddBreakpointDlg.h"

LRESULT CAddBreakpointDlg::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
	AdjustOKCancelButtons(IDI_OK, IDI_DELETE);
	CenterWindow(GetParent());

	return TRUE;
}

LRESULT CAddBreakpointDlg::OnCloseCmd(WORD, WORD wID, HWND, BOOL&) {
	if (wID == IDOK) {
		GetDlgItemText(IDC_BPEXPR, m_Expression);
		m_Expression.Trim();
	}
	EndDialog(wID);
	return 0;
}

LRESULT CAddBreakpointDlg::OnExpressionChanged(WORD, WORD, HWND, BOOL&) {
	CString text;
	GetDlgItemText(IDC_BPEXPR, text);
	text.Trim();
	GetDlgItem(IDOK).EnableWindow(!text.IsEmpty());
	return 0;
}
