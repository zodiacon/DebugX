#include "pch.h"
#include "CommandView.h"
#include "DebugManager.h"
#include "resource.h"

using namespace DebugEngine;
using namespace Scintilla;

CCommandView::CCommandView() : m_CommandEdit(this, 1), m_Text(this, 2) {
}

void CCommandView::OnFinalMessage(HWND) {
	delete this;
}

LRESULT CCommandView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_Text.Create(*this, nullptr, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
		WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, TextId);
	m_Text.InitCall();

	// a read-only, unmargined log: SCI_APPENDTEXT (the only way this view's text ever changes) works
	// regardless of the read-only flag, so this only blocks the user from typing into it
	m_Text->SetReadOnly(true);
	m_Text->SetUndoCollection(false);
	m_Text->SetMarginWidthN(0, 0);
	m_Text->SetMarginWidthN(1, 0);
	m_Text->SetMarginWidthN(2, 0);
	m_Text->SetWrapMode(Wrap::None);
	// (the view's own menu, with Clear Output: OnOutputContextMenu)
	m_Text->UsePopUp(PopUp::Never);
	AppFonts::ApplyCodeFont(m_Text.Call());
	ApplyOutputColors();

	m_CommandEdit.Create(*this, nullptr, nullptr, WS_CHILD | WS_VISIBLE | WS_BORDER);
	m_CommandEdit.SetFont(AppFonts::CodeFont());
	m_CommandEdit.SetLimitText(512);
	m_Prompt.Create(*this, nullptr, nullptr, WS_CHILD | WS_VISIBLE | SS_SUNKEN | SS_RIGHT);
	m_Prompt.SetFont(AppFonts::CodeFont());
	m_Execute.Create(*this, nullptr, L"Exec", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_FLAT | WS_BORDER, 0, ExecuteId);
	m_Execute.SetFont(AppFonts::UiFont());		// the UI font, like the Memory pane's "Go" button, not the code font


	DebugManager::Session().AddObserver(this);
	UpdateControls();

	return 0;
}

HWND CCommandView::Create(HWND hParent) {
	return __super::Create(hParent, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
}

void CCommandView::OnSessionChanged() {
	UpdateControls();
}

void CCommandView::UpdateControls() {
	auto& session = DebugManager::Session();
	auto busy = session.IsBusy();
	if (busy != m_IsBusy)
		SetBusy(busy);

	auto ready = !busy && session.IsBroken();
	bool enabled = false;
	if (ready != (m_CommandEdit.IsWindowEnabled() != FALSE)) {
		m_CommandEdit.EnableWindow(ready);
		m_Execute.EnableWindow(ready);
		enabled = ready;
	}
	if (!session.HasTarget())
		m_RestoreFocus = false;		// (the command ended the session: not for the next one)
	if (ready && (enabled || m_RestoreFocus))
		RestoreFocus(std::exchange(m_RestoreFocus, false));
}

void CCommandView::RestoreFocus(bool commandFromHere) {
	// not taken from another window the user has gone to meanwhile, nor from another program (the target stopped while
	// the user was elsewhere), nor given to a view that cannot be seen (another document's tab is in front)
	auto foreground = ::GetForegroundWindow();
	if (!IsWindowVisible() || (foreground != GetAncestor(m_hWnd, GA_ROOT) && foreground != GetAncestor(m_hWnd, GA_ROOTOWNER)))
		return;
	// from nowhere: where it went when the edit was disabled while it had it (a command ran, or a session started from
	// a dialog that is gone); from this view, for a command run here (the output's link that ran it)
	auto focus = ::GetFocus();
	if (focus == nullptr || focus == m_CommandEdit || (commandFromHere && (focus == m_hWnd || IsChild(focus))))
		m_CommandEdit.SetFocus();
}

LRESULT CCommandView::OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
	DebugManager::Session().RemoveObserver(this);

	handled = FALSE;

	return 0;
}

LRESULT CCommandView::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto cx = GET_X_LPARAM(lParam), cy = GET_Y_LPARAM(lParam);
	if (cx == 0 || cy == 0)
		return 0;

	if (m_Text) {
		// (as big as the fonts need)
		int cmdHeight = std::max({ 20, AppFonts::TextHeight(AppFonts::CodeFont()) + 6, AppFonts::TextHeight(AppFonts::UiFont()) + 6 });
		int buttonWidth = std::max(60, AppFonts::TextWidth(AppFonts::UiFont(), L"Exec") + 28);
		int promptWidth = std::max(60, AppFonts::TextWidth(AppFonts::CodeFont(), L"0:000> ") + 8);
		m_Text.MoveWindow(0, 0, cx, cy - cmdHeight);
		m_Prompt.MoveWindow(0, cy - cmdHeight + 1, promptWidth, cmdHeight - 2);
		m_CommandEdit.MoveWindow(2 + promptWidth, cy - cmdHeight, cx - promptWidth - buttonWidth - 4, cmdHeight);
		m_Execute.MoveWindow(cx - buttonWidth + 2, cy - cmdHeight, buttonWidth - 4, cmdHeight);
	}
	return 0;
}

void CCommandView::OnOutput(const OutputData& data) {
	if (data.Text.empty()) {
		// flush
		ScrollToEnd();
		return;
	}

	CString text(data.Text.c_str());

	if ((data.Mask & OutputDataMask::Prompt) == OutputDataMask::Prompt) {
		m_PromptText = text;
		if (!m_IsBusy)
			m_Prompt.SetWindowText(text);
	}
	else {
		if (data.IsDml)
			AppendDmlText(text);
		else
			AppendText((PCWSTR)text);
		ScrollToEnd();
	}
}

// the system colors are the dark ones while in dark mode
LRESULT CCommandView::OnThemeChanged(UINT, WPARAM, LPARAM, BOOL&) {
	ApplyOutputColors();
	// the scroll bars follow the visual style
	::SetWindowTheme(m_Text, WTLHelper::IsDarkMode() ? L"DarkMode_Explorer" : L"Explorer", nullptr);
	m_Text.Invalidate();
	return 0;
}

LRESULT CCommandView::OnFontsChanged(UINT, WPARAM wParam, LPARAM, BOOL&) {
	if (static_cast<AppFonts::Kind>(wParam) == AppFonts::Kind::Code) {
		AppFonts::ApplyCodeFont(m_Text.Call());
		ApplyOutputColors();		// (StyleClearAll: the font to every style)
		m_CommandEdit.SetFont(AppFonts::CodeFont());
		m_Prompt.SetFont(AppFonts::CodeFont());
	}
	else
		m_Execute.SetFont(AppFonts::UiFont());
	CRect rc;
	GetClientRect(&rc);
	SendMessage(WM_SIZE, SIZE_RESTORED, MAKELPARAM(rc.Width(), rc.Height()));
	return 0;
}

void CCommandView::ApplyOutputColors() {
	auto fore = static_cast<Colour>(::GetSysColor(COLOR_WINDOWTEXT));
	auto back = static_cast<Colour>(::GetSysColor(COLOR_WINDOW));

	// StylesCommon::Default is the template SCI_STYLECLEARALL copies into every style, including
	// style 0 (plain text, since this view has no lexer); reapply the link style afterwards, which
	// StyleClearAll just overwrote back to plain
	m_Text->StyleSetFore(static_cast<int>(StylesCommon::Default), fore);
	m_Text->StyleSetBack(static_cast<int>(StylesCommon::Default), back);
	m_Text->StyleClearAll();
	m_Text->SetCaretFore(fore);
	m_Text->SetSelBack(true, static_cast<Colour>(::GetSysColor(COLOR_HIGHLIGHT)));

	m_Text->StyleSetFore(StyleLink, static_cast<Colour>(WTLHelper::IsDarkMode() ? RGB(102, 178, 255) : RGB(0, 0, 255)));
	m_Text->StyleSetUnderline(StyleLink, true);
	m_Text->StyleSetHotSpot(StyleLink, true);
}

LRESULT CCommandView::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
	m_CommandEdit.SetFocus();
	return 0;
}

LRESULT CCommandView::OnExecuteCommand(WORD, WORD, HWND, BOOL&) {
	DoExecuteCommand();
	return 0;
}

void CCommandView::DoExecuteCommand(PCWSTR command) {
	CString cmd;
	if (command == nullptr) {
		m_CommandEdit.GetWindowText(cmd);
		if (cmd.IsEmpty())
			return;
	}
	else {
		cmd = command;
	}

	// (the same command again is not kept twice in a row; the oldest go past MaxHistory)
	if (m_CommandHistory.empty() || m_CommandHistory.back() != cmd)
		m_CommandHistory.push_back(cmd);
	if (m_CommandHistory.size() > MaxHistory)
		m_CommandHistory.erase(m_CommandHistory.begin(), m_CommandHistory.end() - MaxHistory);
	m_CommandHistoryIndex = (int)m_CommandHistory.size();
	m_CommandEdit.SetWindowText(L"");

	// WinDbg's .cls is the window's, not the engine's
	CString trimmed(cmd);
	trimmed.Trim();
	if (trimmed.CompareNoCase(L".cls") == 0) {
		ClearOutput();
		return;
	}

	AppendText((PCWSTR)(m_PromptText + L" " + cmd + L"\n"));
	ScrollToEnd();
	m_RestoreFocus = true;
	m_CommandEdit.EnableWindow(FALSE);
	m_Execute.EnableWindow(FALSE);
	RunCommand(cmd);
}

Task<void> CCommandView::RunCommand(CString command) {
	auto alive = m_Lifetime.Watch();
	co_await DebugManager::Debugger().ExecuteCommand(command);
	if (!*alive)
		co_return;

	// (the focus comes back to the edit when it is enabled: now, or when the target stops)
	UpdateControls();
}

void CCommandView::AppendText(std::wstring_view text) {
	auto utf8 = ToUtf8(text);
	// unlike RichEdit's AppendText, Scintilla's honors SCI_SETREADONLY (Document::InsertString checks
	// it), so read-only has to come off for this call -- toggled back right after, since the read-only
	// state is what actually stops the user from typing into the view (they never get keyboard focus
	// here in normal use, but the control itself doesn't know that)
	m_Text->SetReadOnly(false);
	m_Text->AppendText(static_cast<Position>(utf8.size()), utf8.c_str());
	m_Text->SetReadOnly(true);
}

void CCommandView::ScrollToEnd() {
	m_Text->DocumentEnd();
	m_Text->ScrollCaret();
}

// dbgeng's DML: plain text with <link cmd="...">text</link> runs (and <altlink ...>, dropped for now,
// same as before). The plain text is appended once; the link runs are recorded in bytes as they are
// appended (the view is UTF-8 internally) and styled with StyleLink afterwards.
void CCommandView::AppendDmlText(CString& dml) {
	for (;;) {
		int altStart = dml.Find(L"<altlink");
		if (altStart >= 0) {
			int altEnd = dml.Find(L'>', altStart + 5);
			ATLASSERT(altEnd > altStart);
			dml = dml.Left(altStart) + dml.Mid(altEnd + 1);
		}
		else
			break;
	}

	std::vector<LinkRun> newLinks;

	int index = dml.Find(L'<');
	if (index < 0) {
		AppendText((PCWSTR)dml);
		return;
	}
	if (index > 0)
		AppendText((PCWSTR)dml.Left(index));

	for (;;) {
		auto current = index;
		index = dml.Find(L'<', index);
		if (index < 0) {
			AppendText((PCWSTR)dml.Mid(current + 1));
			break;
		}
		else if (index > current) {
			AppendText((PCWSTR)dml.Mid(current + 1, index - current - 1));
		}
		CString command;
		int cmdStart = dml.Find(L"cmd=\"", index + 1);
		if (cmdStart >= 0) {
			int cmdEnd = dml.Find(L'\"', cmdStart + 5);
			if (cmdEnd > cmdStart)
				command = dml.Mid(cmdStart + 5, cmdEnd - cmdStart - 5);
		}

		int end = dml.Find(L'>', index + 1);
		ATLASSERT(end > index);
		int index2 = dml.Find(L'<', end + 1);
		ATLASSERT(index2 > end);
		CString text = dml.Mid(end + 1, index2 - end - 1);
		text.Replace(L' ', L'_');

		auto start = m_Text->Length();
		AppendText((PCWSTR)text);
		if (!command.IsEmpty())
			newLinks.push_back({ start, m_Text->Length(), command });

		index = dml.Find(L'>', index2 + 1);
	}

	for (auto& link : newLinks) {
		m_Text->StartStyling(link.Start, 0);
		m_Text->SetStyling(link.End - link.Start, StyleLink);
	}
	m_Links.insert(m_Links.end(), newLinks.begin(), newLinks.end());
}

void CCommandView::SetBusy(bool busy) {
	m_IsBusy = busy;
	// the engine's own prompt comes back with the next output; until then show the last one
	m_Prompt.SetWindowText(busy ? CString(L"*BUSY*") : m_PromptText);
}

LRESULT CCommandView::OnEditChar(UINT, WPARAM wParam, LPARAM, BOOL& handled) {
	if (wParam == VK_RETURN && !m_IsBusy) {
		DoExecuteCommand();
	}
	else {
		handled = FALSE;
	}
	return 0;
}

void CCommandView::ClearOutput() {
	m_Text->SetReadOnly(false);
	m_Text->ClearAll();
	m_Text->SetReadOnly(true);
	// (the links are by position in the text)
	m_Links.clear();
}

LRESULT CCommandView::OnOutputContextMenu(UINT, WPARAM, LPARAM lParam, BOOL&) {
	CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	if (pt.x == -1 && pt.y == -1) {
		// (the keyboard's menu key: at the caret)
		auto pos = m_Text->CurrentPos();
		pt.SetPoint(m_Text->PointXFromPosition(pos), m_Text->PointYFromPosition(pos));
		m_Text.ClientToScreen(&pt);
	}
	enum { CmdCopy = 1, CmdSelectAll, CmdClear };
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(m_Text->SelectionEmpty() ? MF_GRAYED : MF_STRING, CmdCopy, L"&Copy\tCtrl+C");
	menu.AppendMenu(MF_STRING, CmdSelectAll, L"Select &All");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(m_Text->Length() ? MF_STRING : MF_GRAYED, CmdClear, L"C&lear Output\t.cls");
	switch (menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd)) {
		case CmdCopy: m_Text->Copy(); break;
		case CmdSelectAll: m_Text->SelectAll(); break;
		case CmdClear: ClearOutput(); break;
	}
	return 0;
}

std::wstring CCommandView::SaveState() const {
	if (m_CommandHistory.empty())
		return {};
	std::wstring state(L"h:");
	for (size_t i = 0; i < m_CommandHistory.size(); i++) {
		if (i)
			state += L'\t';
		// (a command is one line: no tab or line break of its own goes into the state)
		CString cmd(m_CommandHistory[i]);
		cmd.Replace(L'\t', L' ');
		cmd.Replace(L'\r', L' ');
		cmd.Replace(L'\n', L' ');
		state += cmd;
	}
	return state;
}

void CCommandView::LoadState(PCWSTR state) {
	if (!state || wcsncmp(state, L"h:", 2) != 0)
		return;
	m_CommandHistory.clear();
	for (auto p = state + 2; *p; ) {
		auto end = wcschr(p, L'\t');
		CString cmd(p, end ? static_cast<int>(end - p) : static_cast<int>(wcslen(p)));
		if (!cmd.IsEmpty())
			m_CommandHistory.push_back(cmd);
		if (!end)
			break;
		p = end + 1;
	}
	if (m_CommandHistory.size() > MaxHistory)
		m_CommandHistory.erase(m_CommandHistory.begin(), m_CommandHistory.end() - MaxHistory);
	m_CommandHistoryIndex = static_cast<int>(m_CommandHistory.size());
}

LRESULT CCommandView::OnOutputKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& handled) {
	if (wParam == VK_ESCAPE) {
		m_CommandEdit.SetFocus();
	}
	else {
		handled = FALSE;
	}
	return 0;
}

LRESULT CCommandView::OnEditKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& handled) {
	switch (wParam) {
		case VK_UP:
			if (m_CommandHistoryIndex > 0) {
				m_CommandEdit.SetWindowText(m_CommandHistory[--m_CommandHistoryIndex]);
				m_CommandEdit.SetSel(0, -1);
				m_CommandEdit.SetSel(-1);
			}
			return 0;

		case VK_DOWN:
			if (m_CommandHistoryIndex + 1 < (int)m_CommandHistory.size()) {
				m_CommandEdit.SetWindowText(m_CommandHistory[++m_CommandHistoryIndex]);
				m_CommandEdit.SetSel(0, -1);
				m_CommandEdit.SetSel(-1);
			}
			return 0;

		case VK_ESCAPE:
			m_CommandEdit.SetWindowText(L"");
			return 0;
	}
	handled = FALSE;
	return 0;
}

LRESULT CCommandView::OnHotspotClick(int, LPNMHDR hdr, BOOL&) {
	auto data = reinterpret_cast<const NotificationData*>(hdr);
	for (auto& link : m_Links) {
		if (data->position >= link.Start && data->position < link.End) {
			DoExecuteCommand(link.Command);
			break;
		}
	}
	return 0;
}
