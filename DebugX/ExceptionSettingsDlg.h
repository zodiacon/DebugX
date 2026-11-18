#pragma once

#include <vector>
#include "resource.h"
#include "DialogHelper.h"
#include "Debugger.h"

// Debug > Exception Settings: the engine's event filters -- for each event and exception, whether it breaks (always,
// on the second chance only for an exception), is only said in the Output, or is ignored, and whether an exception is
// passed on to the target as handled or not. Exceptions can be added by code, and the ones added removed.
class CExceptionSettingsDlg :
	public CDialogImpl<CExceptionSettingsDlg>,
	public CDialogHelper<CExceptionSettingsDlg> {
public:
	enum { IDD = IDD_EXCEPTIONS };

	CExceptionSettingsDlg(std::vector<DebugEngine::EventFilter> filters, std::vector<DebugEngine::EventFilter> defaults)
		: m_Filters(std::move(filters)), m_Defaults(std::move(defaults)) {
	}

	BEGIN_MSG_MAP(CExceptionSettingsDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
		COMMAND_HANDLER(IDC_EXBREAK, CBN_SELCHANGE, OnBreakChanged)
		COMMAND_HANDLER(IDC_EXCONTINUE, CBN_SELCHANGE, OnContinueChanged)
		COMMAND_HANDLER(IDC_EXCODE, EN_CHANGE, OnCodeChanged)
		COMMAND_ID_HANDLER(IDC_EXADD, OnAdd)
		COMMAND_ID_HANDLER(IDC_EXREMOVE, OnRemove)
		COMMAND_ID_HANDLER(IDC_EXRESET, OnReset)
		NOTIFY_HANDLER(IDC_EXLIST, LVN_ITEMCHANGED, OnSelectionChanged)
	END_MSG_MAP()

	const std::vector<DebugEngine::EventFilter>& GetFilters() const {
		return m_Filters;
	}

private:
	LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCloseCmd(WORD, WORD wID, HWND, BOOL&);
	LRESULT OnBreakChanged(WORD, WORD, HWND, BOOL&);
	LRESULT OnContinueChanged(WORD, WORD, HWND, BOOL&);
	LRESULT OnCodeChanged(WORD, WORD, HWND, BOOL&);
	LRESULT OnAdd(WORD, WORD, HWND, BOOL&);
	LRESULT OnRemove(WORD, WORD, HWND, BOOL&);
	LRESULT OnReset(WORD, WORD, HWND, BOOL&);
	LRESULT OnSelectionChanged(int, LPNMHDR, BOOL&);

	void FillList();
	void UpdateRow(int row);
	// the combos and buttons as the selected rows are
	void UpdateControls();
	std::vector<int> SelectedRows() const;
	// the code typed in the box, or false
	bool ParseCode(ULONG& code) const;

	CListViewCtrl m_List;
	CComboBox m_Break, m_Continue;
	std::vector<DebugEngine::EventFilter> m_Filters, m_Defaults;
	bool m_Updating{ false };		// the controls are being set, not changed by the user
};
