#pragma once

#include <string>
#include "resource.h"
#include "DialogHelper.h"

// what Find looks for, and how; the frame keeps the last one for F3 / Shift+F3
struct FindOptions {
	std::wstring Text;
	bool MatchCase{ false };
	bool WholeWord{ false };
	bool Up{ false };		// the direction the Find dialog searches in (F3 is always down, Shift+F3 up)
};

// The next match in a Scintilla control (any window whose class has "Scintilla" in it), from the selection on, going
// round past the end (or the start, 'up'); it is selected and scrolled to. 'wrapped': it was found only by going round.
// False if there is none.
bool FindInScintilla(HWND scintilla, const FindOptions& options, bool up, bool* wrapped = nullptr);
// the control's selection, if it is on one line (what Find starts with); empty if not
std::wstring SelectedWord(HWND scintilla);

class CFindDlg :
	public CDialogImpl<CFindDlg>,
	public CDialogHelper<CFindDlg> {
public:
	enum { IDD = IDD_FIND };

	explicit CFindDlg(FindOptions options) : m_Options(std::move(options)) {}

	BEGIN_MSG_MAP(CFindDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
		COMMAND_HANDLER(IDC_FIND_TEXT, EN_CHANGE, OnTextChanged)
	END_MSG_MAP()

	const FindOptions& GetOptions() const {
		return m_Options;
	}

private:
	LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCloseCmd(WORD, WORD wID, HWND, BOOL&);
	LRESULT OnTextChanged(WORD, WORD, HWND, BOOL&);

	FindOptions m_Options;
};
