#include "pch.h"
#include "AssistantSettingsDlg.h"
#include "AppSettings.h"
#include "ApiKey.h"

namespace {
	// (the combo box takes any other model id too)
	PCWSTR const Models[] = { L"claude-opus-5-5", L"claude-sonnet-5-5", L"claude-haiku-5-5", L"claude-fable-5-2" };
	PCWSTR const Efforts[] = { L"low", L"medium", L"high", L"xhigh", L"max" };

	CString Text(CWindow window) {
		CString text;
		window.GetWindowText(text);
		text.Trim();
		return text;
	}
}

LRESULT CAssistantSettingsDlg::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
	AdjustOKCancelButtons(IDI_OK, IDI_DELETE);
	auto& settings = AppSettings::Get();

	CComboBox models(GetDlgItem(IDC_AI_MODEL));
	for (auto m : Models)
		models.AddString(m);
	models.SetWindowText(settings.AssistantModel().c_str());

	CComboBox efforts(GetDlgItem(IDC_AI_EFFORT));
	for (auto e : Efforts)
		efforts.AddString(e);
	if (efforts.SelectString(-1, settings.AssistantEffort().c_str()) < 0)
		efforts.SetCurSel(1);

	PCWSTR status = ApiKey::IsStored() ? L"A key is stored. Type another to replace it."
		: ApiKey::InEnvironment() ? L"No key is stored: ANTHROPIC_API_KEY (the environment's) is used."
		: L"No key yet: the Assistant needs one.";
	SetDlgItemText(IDC_AI_KEYSTATUS, status);
	GetDlgItem(IDC_AI_FORGETKEY).EnableWindow(ApiKey::IsStored());
	CheckDlgButton(IDC_AI_ALLOWCONTROL, settings.AssistantAllowControl() ? BST_CHECKED : BST_UNCHECKED);
	CenterWindow(GetParent());
	if (!ApiKey::IsStored() && !ApiKey::InEnvironment()) {
		GotoDlgCtrl(GetDlgItem(IDC_AI_KEY));
		return FALSE;
	}
	return TRUE;
}

LRESULT CAssistantSettingsDlg::OnCloseCmd(WORD, WORD wID, HWND, BOOL&) {
	if (wID == IDOK) {
		auto model = Text(GetDlgItem(IDC_AI_MODEL));
		if (model.IsEmpty()) {
			AtlMessageBox(m_hWnd, L"Choose a model.", IDS_TITLE, MB_ICONWARNING);
			return 0;
		}
		auto key = Text(GetDlgItem(IDC_AI_KEY));
		if (!key.IsEmpty() && !ApiKey::Store((PCWSTR)key)) {
			AtlMessageBox(m_hWnd, L"The key could not be stored in Windows Credential Manager.", IDS_TITLE, MB_ICONERROR);
			return 0;
		}
		if (key.IsEmpty() && IsDlgButtonChecked(IDC_AI_FORGETKEY) == BST_CHECKED)
			ApiKey::Remove();

		auto& settings = AppSettings::Get();
		settings.AssistantModel((PCWSTR)model);
		settings.AssistantEffort((PCWSTR)Text(GetDlgItem(IDC_AI_EFFORT)));
		settings.AssistantAllowControl(IsDlgButtonChecked(IDC_AI_ALLOWCONTROL) == BST_CHECKED ? 1 : 0);
	}
	EndDialog(wID);
	return 0;
}
