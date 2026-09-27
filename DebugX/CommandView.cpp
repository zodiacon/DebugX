#include "stdafx.h"
#include "CommandView.h"
#include "DebugManager.h"
#include "resource.h"

using namespace DebugEngine;

CCommandView::CCommandView() : m_CommandEdit(this, 1), m_Text(this, 2) {
}

void CCommandView::OnFinalMessage(HWND) {
	delete this;
}

LRESULT CCommandView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_Text.Create(*this, nullptr, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
		WS_CLIPSIBLINGS | WS_CLIPCHILDREN | ES_MULTILINE | ES_READONLY | ES_NOHIDESEL, 0, TextId);

	m_Text.SetAutoURLDetect(TRUE);
	m_Text.SetEventMask(ENM_LINK | ENM_MOUSEEVENTS);

	CHARFORMAT cf = { sizeof(cf) };
	cf.crTextColor = ::GetSysColor(COLOR_WINDOWTEXT);
	cf.dwMask = CFM_COLOR | CFM_FACE;
	::wcscpy_s(cf.szFaceName, L"Consolas");
	m_Text.SetDefaultCharFormat(cf);
	m_Text.SetBackgroundColor(::GetSysColor(COLOR_WINDOW));

	CFontHandle font;
	font.CreatePointFont(90, L"Consolas");

	m_CommandEdit.Create(*this, nullptr, nullptr, WS_CHILD | WS_VISIBLE | WS_BORDER);
	m_CommandEdit.SetFont(font);
	m_CommandEdit.SetLimitText(512);
	m_Prompt.Create(*this, nullptr, nullptr, WS_CHILD | WS_VISIBLE | SS_SUNKEN | SS_RIGHT);
	m_Prompt.SetFont(font);
	m_Execute.Create(*this, nullptr, L"Exec", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_FLAT | WS_BORDER, 0, ExecuteId);

	m_CommandHistory.reserve(64);

	DebugManager::Session().AddObserver(this);
	UpdateControls();

	return 0;
}

LRESULT CCommandView::OnSetCursor(UINT, WPARAM, LPARAM, BOOL&) {
	if (m_hCursor) {
		::SetCursor(m_hCursor);
		return TRUE;
	}
	return FALSE;
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
	if (ready != (m_CommandEdit.IsWindowEnabled() != FALSE)) {
		m_CommandEdit.EnableWindow(ready);
		m_Execute.EnableWindow(ready);
	}
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
		int cmdHeight = 20, buttonWidth = 60, promptWidth = 60;
		m_Text.MoveWindow(0, 0, cx, cy - cmdHeight);
		m_Prompt.MoveWindow(0, cy - cmdHeight + 1, promptWidth, cmdHeight - 2);
		m_CommandEdit.MoveWindow(2 + promptWidth, cy - cmdHeight, cx - promptWidth - buttonWidth - 4, cmdHeight);
		m_Execute.MoveWindow(cx - buttonWidth + 2, cy - 20, buttonWidth - 4, 20);
	}
	return 0;
}

void CCommandView::OnOutput(const OutputData& data) {
	if (data.Text.empty()) {
		// flush
		m_Text.SetSel(-1, 0);
		m_Text.ScrollCaret();
		return;
	}

	CString text(data.Text.c_str());

	if ((data.Mask & OutputDataMask::Prompt) == OutputDataMask::Prompt) {
		m_PromptText = text;
		if (!m_IsBusy)
			m_Prompt.SetWindowText(text);
	}
	else {
		if (data.IsDml) {
			AddDmlText(text);
		}
		else
			m_Text.AppendText(text);
		m_Text.SetSel(-1, 0);
		m_Text.ScrollCaret();
	}
}

// the system colors are the dark ones while in dark mode
LRESULT CCommandView::OnThemeChanged(UINT, WPARAM, LPARAM, BOOL&) {
	CHARFORMAT cf = { sizeof(cf) };
	cf.dwMask = CFM_COLOR;
	cf.crTextColor = ::GetSysColor(COLOR_WINDOWTEXT);
	m_Text.SetDefaultCharFormat(cf);
	m_Text.SetBackgroundColor(::GetSysColor(COLOR_WINDOW));
	// the scroll bars follow the visual style
	::SetWindowTheme(m_Text, WTLHelper::IsDarkMode() ? L"DarkMode_Explorer" : L"Explorer", nullptr);
	m_Text.Invalidate();
	return 0;
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

	m_Text.AppendText(m_PromptText + L" " + cmd + L"\n");
	m_CommandHistory.push_back(cmd);
	m_CommandHistoryIndex = (int)m_CommandHistory.size();
	m_CommandEdit.SetWindowText(L"");
	m_CommandEdit.EnableWindow(FALSE);
	m_Execute.EnableWindow(FALSE);
	RunCommand(cmd);
}

Task<void> CCommandView::RunCommand(CString command) {
	auto alive = m_Lifetime.Watch();
	co_await DebugManager::DebuggerX().ExecuteCommand(command);
	if (!*alive)
		co_return;

	UpdateControls();
	if (m_CommandEdit.IsWindowEnabled())
		m_CommandEdit.SetFocus();
}

void CCommandView::AddDmlText(CString& dml) {
	// remove <altlink markup for now
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

	CHARFORMAT old;
	m_Text.GetSelectionCharFormat(old);

	CHARFORMAT2 cf = { sizeof(cf) };
	cf.dwMask = CFM_EFFECTS | CFM_COLOR | CFM_COOKIE;
	cf.crTextColor = WTLHelper::IsDarkMode() ? RGB(102, 178, 255) : RGB(0, 0, 255);
	cf.dwEffects = CFE_LINK | CFE_UNDERLINE;
	cf.dwCookie = 0x1234;

	int index = dml.Find(L'<');
	if (index > 0)
		m_Text.AppendText(dml.Left(index));
	else if (index < 0) {
		m_Text.AppendText(dml);
		return;
	}

	for (;;) {
		auto current = index;
		index = dml.Find('<', index);
		if (index < 0) {
			m_Text.SetSelectionCharFormat(old);
			m_Text.AppendText(dml.Mid(current + 1));
			break;
		}
		else if(index > current) {
			m_Text.SetSelectionCharFormat(old);
			m_Text.AppendText(dml.Mid(current + 1, index - current - 1));
		}
		CString command;
		int cmdStart = dml.Find(L"cmd=\"", index + 1);
		if (cmdStart >= 0) {
			int cmdEnd = dml.Find(L'\"', cmdStart + 5);
			if (cmdEnd > cmdStart)
				command = dml.Mid(cmdStart + 5, cmdEnd - cmdStart - 5);
		}

		int end = dml.Find('>', index + 1);
		ATLASSERT(end > index);
		int index2 = dml.Find(L'<', end + 1);
		ATLASSERT(index2 > end);
		CString text = dml.Mid(end + 1, index2 - end - 1);
		text.Replace(L' ', L'_');
		m_Text.SetSelNone();
		LONG from, to;
		m_Text.GetSel(from, to);
		if (!command.IsEmpty())
			m_Links.insert({ from, command });
		m_Text.SetSelectionCharFormat(cf);
		m_Text.AppendText(text);

		index = dml.Find(L'>', index2 + 1);
	}
	m_Text.SetSelectionCharFormat(old);
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

LRESULT CCommandView::OnRichEditKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& handled) {
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

LRESULT CCommandView::OnMessageFilter(int, LPNMHDR hdr, BOOL&) {
	auto filter = (MSGFILTER*)hdr;

	if (filter->msg != WM_LBUTTONUP)
		return 0;

	POINT pt = { GET_X_LPARAM(filter->lParam), GET_Y_LPARAM(filter->lParam) };
	auto ch = m_Text.CharFromPos(pt);

	auto left = m_Text.FindWordBreak(WB_LEFT, ch);
	auto delim = m_Text.FindWordBreak(WB_ISDELIMITER, left);
	if (delim)
		left++;

	//WCHAR chr[3];
	//m_Text.GetTextRange(ch, ch + 1, chr);
	//auto right = m_Text.FindWordBreak(WB_RIGHT, left);
	//CString text;
	//m_Text.GetTextRange(left, right + 1, text.GetBufferSetLength(right - left + 1));
	//text.Trim();

	if (auto it = m_Links.find(left); it != m_Links.end()) {
		DoExecuteCommand(it->second);
	}
	return 0;
}

LRESULT CCommandView::OnRichEditSetCursor(UINT, WPARAM, LPARAM lParam, BOOL& handled) {
	if (LOWORD(lParam) == HTCLIENT) {
		m_hCursor = AtlLoadSysCursor(IDC_ARROW);
		POINT pt;
		::GetCursorPos(&pt);
		m_Text.ScreenToClient(&pt);
		auto ch = m_Text.CharFromPos(pt);
		auto left = m_Text.FindWordBreak(WB_LEFT, ch);
		auto delim = m_Text.FindWordBreak(WB_ISDELIMITER, left);
		if (delim)
			left++;

		if (auto it = m_Links.find(left); it != m_Links.end()) {
			::SetCursor(m_hCursor = AtlLoadSysCursor(IDC_HAND));
			return TRUE;
		}
	}
	handled = FALSE;
	return FALSE;
}
