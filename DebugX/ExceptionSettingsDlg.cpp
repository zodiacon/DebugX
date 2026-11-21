#include "pch.h"
#include "ExceptionSettingsDlg.h"

using namespace DebugEngine;

namespace {
	// the "When it happens" combo's items, in order: its index is the engine's execution option
	PCWSTR const ExecutionNames[] = { L"Break", L"Break on second chance", L"Output only", L"Ignore" };
	// the "Continue as" combo's: its index is the engine's continue option
	PCWSTR const ContinueNames[] = { L"Handled", L"Not handled" };

	PCWSTR ExecutionName(const EventFilter& f) {
		return f.ExecutionOption < _countof(ExecutionNames) ? ExecutionNames[f.ExecutionOption] : L"?";
	}
}

LRESULT CExceptionSettingsDlg::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
	AdjustOKCancelButtons(IDI_OK, IDI_DELETE);

	m_List = GetDlgItem(IDC_EXLIST);
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
	// (in dialog units, so that they fit the list whatever the DPI and font)
	CRect units(0, 0, 4, 0);
	MapDialogRect(&units);
	auto width = [&](int dlu) { return ::MulDiv(dlu, units.right, 4); };
	m_List.InsertColumn(0, L"Event / Exception", LVCFMT_LEFT, width(140));
	m_List.InsertColumn(1, L"Code", LVCFMT_LEFT, width(60));
	m_List.InsertColumn(2, L"When it happens", LVCFMT_LEFT, width(100));
	m_List.InsertColumn(3, L"Continue as", LVCFMT_LEFT, width(60));

	m_Break = GetDlgItem(IDC_EXBREAK);
	for (auto name : ExecutionNames)
		m_Break.AddString(name);
	m_Continue = GetDlgItem(IDC_EXCONTINUE);
	for (auto name : ContinueNames)
		m_Continue.AddString(name);

	FillList();
	UpdateControls();
	GetDlgItem(IDC_EXADD).EnableWindow(FALSE);		// (until there is a code to add)
	CenterWindow(GetParent());
	return TRUE;
}

void CExceptionSettingsDlg::FillList() {
	m_List.DeleteAllItems();
	for (int i = 0; i < static_cast<int>(m_Filters.size()); i++) {
		m_List.InsertItem(i, L"");
		UpdateRow(i);
	}
}

void CExceptionSettingsDlg::UpdateRow(int row) {
	auto& f = m_Filters[row];
	CString code;
	if (f.IsException && f.Code == 0 && !f.IsArbitrary)
		code = L"(any other)";		// the engine's "Unknown exception": for every code without a filter of its own
	else if (f.IsException)
		code.Format(L"0x%08X", f.Code);
	m_List.SetItemText(row, 0, f.Name.empty() ? (f.IsException ? L"(added)" : L"?") : f.Name.c_str());
	m_List.SetItemText(row, 1, code);
	m_List.SetItemText(row, 2, ExecutionName(f));
	m_List.SetItemText(row, 3, f.IsException ? ContinueNames[f.ContinueOption ? 1 : 0] : L"");
}

std::vector<int> CExceptionSettingsDlg::SelectedRows() const {
	std::vector<int> rows;
	for (int i = m_List.GetNextItem(-1, LVNI_SELECTED); i >= 0; i = m_List.GetNextItem(i, LVNI_SELECTED))
		rows.push_back(i);
	return rows;
}

void CExceptionSettingsDlg::UpdateControls() {
	m_Updating = true;
	auto rows = SelectedRows();
	bool anyException = false, allArbitrary = !rows.empty();
	int execution = -2, cont = -2;		// -2: none seen yet, -1: they differ
	for (auto row : rows) {
		auto& f = m_Filters[row];
		execution = execution == -2 || execution == static_cast<int>(f.ExecutionOption) ? static_cast<int>(f.ExecutionOption) : -1;
		if (f.IsException) {
			anyException = true;
			cont = cont == -2 || cont == static_cast<int>(f.ContinueOption) ? static_cast<int>(f.ContinueOption) : -1;
		}
		allArbitrary = allArbitrary && f.IsArbitrary;
	}
	m_Break.EnableWindow(!rows.empty());
	m_Break.SetCurSel(execution >= 0 ? execution : -1);
	m_Continue.EnableWindow(anyException);
	m_Continue.SetCurSel(cont >= 0 ? cont : -1);
	GetDlgItem(IDC_EXREMOVE).EnableWindow(allArbitrary);
	m_Updating = false;
}

LRESULT CExceptionSettingsDlg::OnSelectionChanged(int, LPNMHDR hdr, BOOL&) {
	auto lv = reinterpret_cast<NMLISTVIEW*>(hdr);
	if (!m_Updating && (lv->uChanged & LVIF_STATE))
		UpdateControls();
	return 0;
}

LRESULT CExceptionSettingsDlg::OnBreakChanged(WORD, WORD, HWND, BOOL&) {
	if (m_Updating || m_Break.GetCurSel() < 0)
		return 0;

	const ULONG execution = m_Break.GetCurSel();
	for (auto row : SelectedRows()) {
		auto& f = m_Filters[row];
		// (an event has no second chance: it just breaks)
		f.ExecutionOption = !f.IsException && execution == FilterSecondChanceBreak ? FilterBreak : execution;
		UpdateRow(row);
	}
	UpdateControls();
	return 0;
}

LRESULT CExceptionSettingsDlg::OnContinueChanged(WORD, WORD, HWND, BOOL&) {
	if (m_Updating || m_Continue.GetCurSel() < 0)
		return 0;

	for (auto row : SelectedRows()) {
		auto& f = m_Filters[row];
		if (f.IsException) {
			f.ContinueOption = m_Continue.GetCurSel();
			UpdateRow(row);
		}
	}
	return 0;
}

bool CExceptionSettingsDlg::ParseCode(ULONG& code) const {
	CString text;
	GetDlgItemText(IDC_EXCODE, text);
	text.Trim();
	if (text.Left(2).CompareNoCase(L"0x") == 0)
		text = text.Mid(2);
	if (text.IsEmpty() || text.GetLength() > 8)
		return false;
	wchar_t* end;
	code = ::wcstoul(text, &end, 16);
	return *end == 0;
}

LRESULT CExceptionSettingsDlg::OnCodeChanged(WORD, WORD, HWND, BOOL&) {
	ULONG code;
	GetDlgItem(IDC_EXADD).EnableWindow(ParseCode(code));
	return 0;
}

LRESULT CExceptionSettingsDlg::OnAdd(WORD, WORD, HWND, BOOL&) {
	ULONG code;
	if (!ParseCode(code))
		return 0;

	// one that is there already is just selected
	auto it = std::find_if(m_Filters.begin(), m_Filters.end(), [&](auto& f) { return f.IsException && f.Code == code; });
	int row;
	if (it != m_Filters.end())
		row = static_cast<int>(it - m_Filters.begin());
	else {
		EventFilter f;
		f.IsException = f.IsArbitrary = true;
		f.Code = code;
		f.ExecutionOption = FilterBreak;
		m_Filters.push_back(f);
		row = static_cast<int>(m_Filters.size()) - 1;
		m_List.InsertItem(row, L"");
		UpdateRow(row);
	}
	m_List.SetItemState(-1, 0, LVIS_SELECTED);
	m_List.SelectItem(row);
	m_List.SetFocus();
	SetDlgItemText(IDC_EXCODE, L"");
	return 0;
}

LRESULT CExceptionSettingsDlg::OnRemove(WORD, WORD, HWND, BOOL&) {
	auto rows = SelectedRows();
	// (from the end: the indices before it stay good)
	for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
		if (m_Filters[*it].IsArbitrary) {
			m_Filters.erase(m_Filters.begin() + *it);
			m_List.DeleteItem(*it);
		}
	}
	UpdateControls();
	return 0;
}

LRESULT CExceptionSettingsDlg::OnReset(WORD, WORD, HWND, BOOL&) {
	m_Filters = m_Defaults;
	FillList();
	UpdateControls();
	return 0;
}

LRESULT CExceptionSettingsDlg::OnCloseCmd(WORD, WORD wID, HWND, BOOL&) {
	EndDialog(wID);
	return 0;
}
