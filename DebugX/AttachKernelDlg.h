#pragma once

#include "resource.h"
#include "DialogHelper.h"

// asks for the connection string of a kernel debugging session (net:..., com:..., as windbg -k takes it)
class CAttachKernelDlg :
	public CDialogImpl<CAttachKernelDlg>,
	public CDialogHelper<CAttachKernelDlg> {
public:
	enum { IDD = IDD_ATTACHKERNEL };

	explicit CAttachKernelDlg(PCWSTR connection = nullptr) : m_Connection(connection) {}

	BEGIN_MSG_MAP(CAttachKernelDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
		COMMAND_HANDLER(IDC_KERNELCONN, EN_CHANGE, OnConnectionChanged)
	END_MSG_MAP()

	const CString& GetConnection() const {
		return m_Connection;
	}

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnConnectionChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	CString m_Connection;
};
