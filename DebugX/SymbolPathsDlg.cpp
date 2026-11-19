#include "stdafx.h"
#include "SymbolPathsDlg.h"

namespace {
	// a local cache, then Microsoft's server: what windbg's .symfix sets
	PCWSTR const MicrosoftSymbolServer = L"srv*C:\\Symbols*https://msdl.microsoft.com/download/symbols";

	std::wstring Trim(std::wstring_view text) {
		auto first = text.find_first_not_of(L" \t\r\n");
		if (first == std::wstring_view::npos)
			return {};
		auto last = text.find_last_not_of(L" \t\r\n");
		return std::wstring(text.substr(first, last - first + 1));
	}

	std::vector<std::wstring> Split(const std::wstring& text, PCWSTR separators) {
		std::vector<std::wstring> parts;
		size_t pos = 0;
		while (pos <= text.size()) {
			auto end = text.find_first_of(separators, pos);
			if (end == std::wstring::npos)
				end = text.size();
			if (auto part = Trim(std::wstring_view(text).substr(pos, end - pos)); !part.empty())
				parts.push_back(std::move(part));
			pos = end + 1;
		}
		return parts;
	}

	CString GetText(CWindow edit) {
		CString text;
		edit.GetWindowText(text);
		return text;
	}
}

std::wstring CSymbolPathsDlg::ToLines(const std::wstring& path) {
	std::wstring lines;
	for (auto& part : Split(path, L";")) {
		if (!lines.empty())
			lines += L"\r\n";
		lines += part;
	}
	return lines;
}

std::wstring CSymbolPathsDlg::FromLines(const std::wstring& lines) {
	std::wstring path;
	// (a line may itself hold several entries, as pasted from somewhere)
	for (auto& part : Split(lines, L"\r\n;")) {
		if (!path.empty())
			path += L';';
		path += part;
	}
	return path;
}

LRESULT CSymbolPathsDlg::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
	AdjustOKCancelButtons(IDI_OK, IDI_DELETE);
	SetDlgItemText(IDC_SYMPATH, ToLines(m_SymbolPath).c_str());
	SetDlgItemText(IDC_SRCPATH, ToLines(m_SourcePath).c_str());
	CenterWindow(GetParent());
	return TRUE;
}

LRESULT CSymbolPathsDlg::OnCloseCmd(WORD, WORD wID, HWND, BOOL&) {
	if (wID == IDOK) {
		m_SymbolPath = FromLines((PCWSTR)GetText(GetDlgItem(IDC_SYMPATH)));
		m_SourcePath = FromLines((PCWSTR)GetText(GetDlgItem(IDC_SRCPATH)));
	}
	EndDialog(wID);
	return 0;
}

void CSymbolPathsDlg::AppendLine(UINT id, const std::wstring& line) {
	auto edit = GetDlgItem(id);
	std::wstring text = (PCWSTR)GetText(edit);
	for (auto& part : Split(text, L"\r\n;")) {
		if (_wcsicmp(part.c_str(), line.c_str()) == 0)
			return;
	}
	text = Trim(text);
	if (!text.empty())
		text += L"\r\n";
	text += line;
	edit.SetWindowText(text.c_str());
	edit.SetFocus();
	CEdit(edit).SetSel(static_cast<int>(text.size()), static_cast<int>(text.size()));
}

LRESULT CSymbolPathsDlg::OnAddSymbolServer(WORD, WORD, HWND, BOOL&) {
	// (a srv* entry that is there already, whatever its cache, is left as it is)
	for (auto& part : Split((PCWSTR)GetText(GetDlgItem(IDC_SYMPATH)), L"\r\n;")) {
		if (_wcsnicmp(part.c_str(), L"srv*", 4) == 0 && part.find(L"msdl.microsoft.com") != std::wstring::npos) {
			::MessageBeep(MB_ICONINFORMATION);
			return 0;
		}
	}
	AppendLine(IDC_SYMPATH, MicrosoftSymbolServer);
	return 0;
}

LRESULT CSymbolPathsDlg::OnAddFolder(WORD, WORD wID, HWND, BOOL&) {
	CShellFileOpenDialog dlg(nullptr, FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
	dlg.GetPtr()->SetTitle(wID == IDC_SYMFOLDER ? L"Folder with symbol files" : L"Folder with source files");
	// the system's picker does not take the dark mode hook
	WTLHelper::SuspendHook();
	auto ok = dlg.DoModal(m_hWnd) == IDOK;
	WTLHelper::ResumeHook();
	if (!ok)
		return 0;

	CString folder;
	if (SUCCEEDED(dlg.GetFilePath(folder)))
		AppendLine(wID == IDC_SYMFOLDER ? IDC_SYMPATH : IDC_SRCPATH, (PCWSTR)folder);
	return 0;
}
