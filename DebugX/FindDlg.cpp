#include "pch.h"
#include "FindDlg.h"
#include "ScintillaCtrl.h"

namespace {
	LRESULT Sci(HWND h, UINT msg, WPARAM w = 0, LPARAM l = 0) {
		return ::SendMessage(h, msg, w, l);
	}

	// the match in [start, end) -- backwards when start > end --, selected; false if there is none
	bool SearchRange(HWND h, const std::string& text, Scintilla::Position start, Scintilla::Position end) {
		Sci(h, SCI_SETTARGETRANGE, start, end);
		if (Sci(h, SCI_SEARCHINTARGET, text.size(), reinterpret_cast<LPARAM>(text.c_str())) < 0)
			return false;
		auto from = Sci(h, SCI_GETTARGETSTART), to = Sci(h, SCI_GETTARGETEND);
		// (the caret at the end of it, which is what is scrolled into view)
		Sci(h, SCI_SETSEL, from, to);
		return true;
	}
}

bool FindInScintilla(HWND h, const FindOptions& options, bool up, bool* wrapped) {
	if (wrapped)
		*wrapped = false;
	if (!h || options.Text.empty())
		return false;

	auto text = ToUtf8(options.Text);
	int flags = 0;
	if (options.MatchCase)
		flags |= SCFIND_MATCHCASE;
	if (options.WholeWord)
		flags |= SCFIND_WHOLEWORD;
	Sci(h, SCI_SETSEARCHFLAGS, flags);

	auto length = Sci(h, SCI_GETLENGTH);
	// (from past the selection, so that the same match is not found again)
	if (!up) {
		auto start = Sci(h, SCI_GETSELECTIONEND);
		if (SearchRange(h, text, start, length))
			return true;
		if (wrapped)
			*wrapped = true;
		return SearchRange(h, text, 0, start);
	}
	auto start = Sci(h, SCI_GETSELECTIONSTART);
	if (SearchRange(h, text, start, 0))
		return true;
	if (wrapped)
		*wrapped = true;
	return SearchRange(h, text, length, start);
}

std::wstring SelectedWord(HWND h) {
	auto start = Sci(h, SCI_GETSELECTIONSTART), end = Sci(h, SCI_GETSELECTIONEND);
	if (start == end || end - start > 256 || Sci(h, SCI_LINEFROMPOSITION, start) != Sci(h, SCI_LINEFROMPOSITION, end))
		return {};
	std::string text(end - start + 1, '\0');
	Sci(h, SCI_GETSELTEXT, 0, reinterpret_cast<LPARAM>(text.data()));
	text.resize(strlen(text.c_str()));
	return FromUtf8(text);
}

LRESULT CFindDlg::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
	AdjustOKCancelButtons(IDI_FIND, IDI_DELETE);
	SetDlgItemText(IDC_FIND_TEXT, m_Options.Text.c_str());
	CheckDlgButton(IDC_FIND_MATCHCASE, m_Options.MatchCase ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_FIND_WHOLEWORD, m_Options.WholeWord ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_FIND_UP, m_Options.Up ? BST_CHECKED : BST_UNCHECKED);
	GetDlgItem(IDOK).EnableWindow(!m_Options.Text.empty());
	CenterWindow(GetParent());

	GotoDlgCtrl(GetDlgItem(IDC_FIND_TEXT));
	return FALSE;
}

LRESULT CFindDlg::OnCloseCmd(WORD, WORD wID, HWND, BOOL&) {
	if (wID == IDOK) {
		CString text;
		GetDlgItemText(IDC_FIND_TEXT, text);
		m_Options.Text = (PCWSTR)text;
		m_Options.MatchCase = IsDlgButtonChecked(IDC_FIND_MATCHCASE) == BST_CHECKED;
		m_Options.WholeWord = IsDlgButtonChecked(IDC_FIND_WHOLEWORD) == BST_CHECKED;
		m_Options.Up = IsDlgButtonChecked(IDC_FIND_UP) == BST_CHECKED;
	}
	EndDialog(wID);
	return 0;
}

LRESULT CFindDlg::OnTextChanged(WORD, WORD, HWND, BOOL&) {
	GetDlgItem(IDOK).EnableWindow(GetDlgItem(IDC_FIND_TEXT).GetWindowTextLength() > 0);
	return 0;
}
