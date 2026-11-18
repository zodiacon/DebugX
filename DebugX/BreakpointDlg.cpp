#include "stdafx.h"
#include "BreakpointDlg.h"

using namespace DebugEngine;

namespace {
	const ULONG DataSizes[] = { 1, 2, 4, 8 };
	const struct {
		ULONG Access;
		PCWSTR Name;
	} DataAccesses[] = {
		{ DEBUG_BREAK_WRITE, L"Write" },
		{ DEBUG_BREAK_READ, L"Read or write" },
		{ DEBUG_BREAK_EXECUTE, L"Execute" },
	};
}

CString BreakpointErrorText(const BreakpointSettings& settings, HRESULT hr) {
	CString text;
	if (settings.IsData && hr == HRESULT_FROM_WIN32(ERROR_INVALID_ADDRESS))
		text.Format(L"A data breakpoint of %u bytes must be at an address that is a multiple of %u (%s is not)",
			settings.DataSize, settings.DataSize, settings.Expression.c_str());
	else if (hr == E_INVALIDARG)
		text.Format(L"The breakpoint at %s cannot be set that way (a code breakpoint cannot become a data breakpoint, or the other way around)",
			settings.Expression.c_str());
	else
		text.Format(L"Failed to set a %s breakpoint at %s (0x%08X)", settings.IsData ? L"data" : L"code",
			settings.Expression.c_str(), static_cast<unsigned>(hr));
	return text;
}

CBreakpointDlg::CBreakpointDlg(const BreakpointSettings* existing) : m_Editing(existing != nullptr) {
	if (existing)
		m_Settings = *existing;
}

CString CBreakpointDlg::GetText(UINT id) const {
	CString text;
	GetDlgItemText(id, text);
	text.Trim();
	return text;
}

LRESULT CBreakpointDlg::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
	AdjustOKCancelButtons(IDI_OK, IDI_DELETE);
	SetWindowText(m_Editing ? L"Edit Breakpoint" : L"New Breakpoint");

	CheckRadioButton(IDC_BPCODE, IDC_BPDATA, m_Settings.IsData ? IDC_BPDATA : IDC_BPCODE);
	// (an engine breakpoint is a code one or a data one for good)
	GetDlgItem(IDC_BPCODE).EnableWindow(!m_Editing);
	GetDlgItem(IDC_BPDATA).EnableWindow(!m_Editing);

	CComboBox sizes(GetDlgItem(IDC_BPSIZE));
	for (auto size : DataSizes) {
		CString text;
		text.Format(L"%u", size);
		auto index = sizes.AddString(text);
		if (size == m_Settings.DataSize)
			sizes.SetCurSel(index);
	}
	if (sizes.GetCurSel() < 0)
		sizes.SetCurSel(0);

	CComboBox accesses(GetDlgItem(IDC_BPACCESS));
	for (auto& a : DataAccesses) {
		auto index = accesses.AddString(a.Name);
		if (a.Access == m_Settings.DataAccess)
			accesses.SetCurSel(index);
	}
	if (accesses.GetCurSel() < 0)
		accesses.SetCurSel(0);

	SetDlgItemText(IDC_BPEXPR, m_Settings.Expression.c_str());
	SetDlgItemText(IDC_BPCONDITION, m_Settings.Condition.c_str());
	SetDlgItemText(IDC_BPCOMMAND, m_Settings.Command.c_str());
	SetDlgItemInt(IDC_BPPASSCOUNT, m_Settings.PassCount ? m_Settings.PassCount : 1, FALSE);
	if (m_Settings.ThreadId != DEBUG_ANY_ID)
		SetDlgItemInt(IDC_BPTHREAD, m_Settings.ThreadId, FALSE);
	CheckDlgButton(IDC_BPENABLED, m_Settings.Enabled ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_BPONESHOT, m_Settings.OneShot ? BST_CHECKED : BST_UNCHECKED);

	UpdateType();
	GetDlgItem(IDOK).EnableWindow(!GetText(IDC_BPEXPR).IsEmpty());
	CenterWindow(GetParent());
	GotoDlgCtrl(GetDlgItem(IDC_BPEXPR));
	return FALSE;
}

void CBreakpointDlg::UpdateType() {
	const bool data = IsDlgButtonChecked(IDC_BPDATA) == BST_CHECKED;
	for (auto id : { IDC_BPSIZE, IDC_BPACCESS, IDC_BPSIZELABEL, IDC_BPACCESSLABEL })
		GetDlgItem(id).EnableWindow(data);
	SetDlgItemText(IDC_BPEXPRLABEL, data ? L"&Address:" : L"&Location:");
}

LRESULT CBreakpointDlg::OnTypeChanged(WORD, WORD, HWND, BOOL&) {
	UpdateType();
	return 0;
}

LRESULT CBreakpointDlg::OnExpressionChanged(WORD, WORD, HWND, BOOL&) {
	GetDlgItem(IDOK).EnableWindow(!GetText(IDC_BPEXPR).IsEmpty());
	return 0;
}

LRESULT CBreakpointDlg::OnCloseCmd(WORD, WORD wID, HWND, BOOL&) {
	if (wID == IDOK) {
		m_Settings.IsData = IsDlgButtonChecked(IDC_BPDATA) == BST_CHECKED;
		m_Settings.Expression = GetText(IDC_BPEXPR);
		m_Settings.Condition = GetText(IDC_BPCONDITION);
		m_Settings.Command = GetText(IDC_BPCOMMAND);

		BOOL ok;
		auto passCount = GetDlgItemInt(IDC_BPPASSCOUNT, &ok, FALSE);
		m_Settings.PassCount = ok && passCount > 0 ? passCount : 1;
		auto thread = GetDlgItemInt(IDC_BPTHREAD, &ok, FALSE);
		m_Settings.ThreadId = ok ? thread : DEBUG_ANY_ID;

		m_Settings.Enabled = IsDlgButtonChecked(IDC_BPENABLED) == BST_CHECKED;
		m_Settings.OneShot = IsDlgButtonChecked(IDC_BPONESHOT) == BST_CHECKED;

		auto size = CComboBox(GetDlgItem(IDC_BPSIZE)).GetCurSel();
		m_Settings.DataSize = DataSizes[size >= 0 ? size : 0];
		auto access = CComboBox(GetDlgItem(IDC_BPACCESS)).GetCurSel();
		m_Settings.DataAccess = DataAccesses[access >= 0 ? access : 0].Access;
	}
	EndDialog(wID);
	return 0;
}
