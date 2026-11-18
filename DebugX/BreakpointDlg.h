#pragma once

#include "resource.h"
#include "DialogHelper.h"
#include "Debugger.h"

// what to tell the user when a breakpoint could not be set (AddBreakpoint, UpdateBreakpoint)
CString BreakpointErrorText(const DebugEngine::BreakpointSettings& settings, HRESULT hr);

// A breakpoint, new or to change: code or data, where, its condition, command, hit count, thread, enabled, one-shot.
// (Editing one cannot change its type: the engine's breakpoints are one or the other for good.)
class CBreakpointDlg :
	public CDialogImpl<CBreakpointDlg>,
	public CDialogHelper<CBreakpointDlg> {
public:
	enum { IDD = IDD_BREAKPOINT };

	// 'existing': the breakpoint being edited, or null for a new one
	explicit CBreakpointDlg(const DebugEngine::BreakpointSettings* existing = nullptr);

	BEGIN_MSG_MAP(CBreakpointDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
		COMMAND_HANDLER(IDC_BPEXPR, EN_CHANGE, OnExpressionChanged)
		COMMAND_HANDLER(IDC_BPCODE, BN_CLICKED, OnTypeChanged)
		COMMAND_HANDLER(IDC_BPDATA, BN_CLICKED, OnTypeChanged)
	END_MSG_MAP()

	const DebugEngine::BreakpointSettings& GetSettings() const {
		return m_Settings;
	}

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnExpressionChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnTypeChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// the controls that are only for one type, shown or not
	void UpdateType();
	CString GetText(UINT id) const;

	DebugEngine::BreakpointSettings m_Settings;
	bool m_Editing;
};
