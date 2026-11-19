#pragma once

#include "resource.h"
#include "DialogHelper.h"

// asks for one line of text (Go to Line, Go to Address, ...): a title, a prompt and what is in the box at first
class CInputDlg :
	public CDialogImpl<CInputDlg>,
	public CDialogHelper<CInputDlg> {
public:
	enum { IDD = IDD_INPUT };

	CInputDlg(PCWSTR title, PCWSTR prompt, PCWSTR text = nullptr) : m_Title(title), m_Prompt(prompt), m_Text(text) {}

	BEGIN_MSG_MAP(CInputDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
		COMMAND_HANDLER(IDC_INPUT_TEXT, EN_CHANGE, OnTextChanged)
	END_MSG_MAP()

	// trimmed; never empty after IDOK
	const CString& GetText() const {
		return m_Text;
	}

private:
	LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCloseCmd(WORD, WORD wID, HWND, BOOL&);
	LRESULT OnTextChanged(WORD, WORD, HWND, BOOL&);

	CString m_Title, m_Prompt, m_Text;
};
