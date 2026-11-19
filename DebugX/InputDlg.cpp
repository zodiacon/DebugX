#include "stdafx.h"
#include "InputDlg.h"

LRESULT CInputDlg::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
	AdjustOKCancelButtons(IDI_OK, IDI_DELETE);
	SetWindowText(m_Title);
	SetDlgItemText(IDC_INPUT_PROMPT, m_Prompt);
	SetDlgItemText(IDC_INPUT_TEXT, m_Text);
	CenterWindow(GetParent());

	// the text is all selected, ready to be typed over
	GotoDlgCtrl(GetDlgItem(IDC_INPUT_TEXT));
	return FALSE;
}

LRESULT CInputDlg::OnCloseCmd(WORD, WORD wID, HWND, BOOL&) {
	if (wID == IDOK) {
		GetDlgItemText(IDC_INPUT_TEXT, m_Text);
		m_Text.Trim();
	}
	EndDialog(wID);
	return 0;
}

LRESULT CInputDlg::OnTextChanged(WORD, WORD, HWND, BOOL&) {
	CString text;
	GetDlgItemText(IDC_INPUT_TEXT, text);
	text.Trim();
	GetDlgItem(IDOK).EnableWindow(!text.IsEmpty());
	return 0;
}
