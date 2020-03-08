#include "stdafx.h"
#include "CommandView.h"
#include "DebugManager.h"

using namespace DebugEngine;

CCommandView::CCommandView() : m_CommandEdit(this, 1), m_Text(this, 2) {
}

void CCommandView::OnFinalMessage(HWND) {
	delete this;
}

LRESULT CCommandView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_Text.Create(*this, nullptr, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
		ES_MULTILINE | ES_READONLY);

	CHARFORMAT cf{};
	cf.crTextColor = RGB(0, 0, 0);
	cf.dwMask = CFM_COLOR | CFM_FACE;
	::wcscpy_s(cf.szFaceName, L"Consolas");
	m_Text.SetDefaultCharFormat(cf);
	m_Text.SetAutoURLDetect(TRUE);

	DebugManager::Get()->RegisterWindow(*this);

	CFontHandle font;
	font.CreatePointFont(90, L"Consolas");

	m_CommandEdit.Create(*this, nullptr, nullptr, WS_CHILD | WS_VISIBLE | WS_BORDER);
	m_CommandEdit.SetFont(font);
	m_CommandEdit.SetLimitText(512);
	m_Prompt.Create(*this, nullptr, nullptr, WS_CHILD | WS_VISIBLE | SS_SUNKEN | SS_RIGHT);
	m_Prompt.SetFont(font);
	m_Execute.Create(*this, nullptr, L"Execute", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON);
	m_Execute.SetFont(font);

	m_CommandHistory.reserve(64);

	return 0;
}

LRESULT CCommandView::OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
	DebugManager::Get()->UnregisterWindow(*this);
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
		m_Execute.MoveWindow(cx - buttonWidth + 2, cy - cmdHeight, buttonWidth - 4, cmdHeight);
	}
	return 0;
}

LRESULT CCommandView::OnDebugOutput(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto data = reinterpret_cast<OutputData*>(lParam);
	COLORREF color = 0;
	CString text(data->Text.c_str());

	if ((data->Mask & OutputDataMask::Prompt) == OutputDataMask::Prompt) {
		m_Prompt.SetWindowTextW(text);
	}
	else {
		m_Text.AppendText(text);
		m_Text.SetSel(-1, 0);
		m_Text.ScrollCaret();
	}

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

void CCommandView::DoExecuteCommand() {
	CString cmd;
	m_CommandEdit.GetWindowText(cmd);
	if (cmd.IsEmpty())
		return;

	CString prompt;
	m_Prompt.GetWindowText(prompt);
	m_Text.AppendText(prompt + L" " + cmd + L"\n");
	m_CommandHistory.push_back(cmd);

	m_CommandEdit.SetWindowText(L"");
	m_CommandEdit.EnableWindow(FALSE);
	m_Execute.EnableWindow(FALSE);
	DebugManager::Get()->Debugger().ExecuteCommand(cmd);
	m_CommandEdit.EnableWindow();
	m_Execute.EnableWindow();
	m_CommandEdit.SetFocus();
}

LRESULT CCommandView::OnEditChar(UINT, WPARAM wParam, LPARAM, BOOL& handled) {
	if (wParam == VK_RETURN) {
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
