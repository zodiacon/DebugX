#pragma once

#include "resource.h"
#include "DialogHelper.h"

class CAddBreakpointDlg :
	public CDialogImpl<CAddBreakpointDlg>,
	public CDialogHelper<CAddBreakpointDlg> {
public:
	enum { IDD = IDD_ADDBREAKPOINT };

	BEGIN_MSG_MAP(CAddBreakpointDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
		COMMAND_HANDLER(IDC_BPEXPR, EN_CHANGE, OnExpressionChanged)
	END_MSG_MAP()

	const CString& GetExpression() const {
		return m_Expression;
	}

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnExpressionChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	CString m_Expression;
};
