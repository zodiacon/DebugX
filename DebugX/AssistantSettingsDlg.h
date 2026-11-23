#pragma once

#include "resource.h"
#include "DialogHelper.h"

// The Assistant pane's settings: the model and its effort (AppSettings), the API key (Windows Credential Manager: ApiKey.h),
// and whether its tools that change the session run without asking. OK saves them.
class CAssistantSettingsDlg :
	public CDialogImpl<CAssistantSettingsDlg>,
	public CDialogHelper<CAssistantSettingsDlg> {
public:
	enum { IDD = IDD_ASSISTANTSETTINGS };

	BEGIN_MSG_MAP(CAssistantSettingsDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
	END_MSG_MAP()

private:
	LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCloseCmd(WORD, WORD wID, HWND, BOOL&);
};
