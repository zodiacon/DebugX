#include "pch.h"
#include "OutputView.h"
#include "DebugManager.h"
#include "resource.h"

namespace {
	// what the window keeps: the oldest lines go when there are more
	constexpr int MaxLines = 5000;
	constexpr int TrimLines = 500;
}

HWND COutputView::Create(HWND hParent) {
	return BaseFrame::Create(hParent, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
}

void COutputView::OnFinalMessage(HWND) {
	delete this;
}

LRESULT COutputView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_Edit.Create(*this, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_CLIPSIBLINGS |
		ES_MULTILINE | ES_READONLY | ES_NOHIDESEL | ES_AUTOVSCROLL | ES_AUTOHSCROLL);
	m_Edit.SetLimitText(0);
	m_Font.CreatePointFont(90, L"Consolas");
	m_Edit.SetFont(m_Font);

	// everything so far (the window may be made long after the target started), then what comes
	auto& session = DebugManager::Session();
	CString text;
	for (auto& line : session.Log())
		text += CString(line.c_str()) + L"\r\n";
	m_Edit.SetWindowText(text);
	m_Edit.SetSel(-1, -1);
	m_Edit.ScrollCaret();
	session.AddObserver(this);

	return 0;
}

LRESULT COutputView::OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
	DebugManager::Session().RemoveObserver(this);
	handled = FALSE;
	return 0;
}

LRESULT COutputView::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
	m_Edit.SetFocus();
	return 0;
}

void COutputView::OnLogLine(const std::wstring& line) {
	if (m_Edit.GetLineCount() > MaxLines) {
		m_Edit.SetRedraw(FALSE);
		m_Edit.SetSel(0, m_Edit.LineIndex(TrimLines));
		m_Edit.ReplaceSel(L"");
		m_Edit.SetRedraw(TRUE);
	}
	m_Edit.SetSel(-1, -1);
	m_Edit.ReplaceSel(CString(line.c_str()) + L"\r\n");
	m_Edit.ScrollCaret();
}

LRESULT COutputView::OnClear(WORD, WORD, HWND, BOOL&) {
	m_Edit.SetWindowText(L"");
	return 0;
}

LRESULT COutputView::OnCopy(WORD, WORD, HWND, BOOL&) {
	m_Edit.Copy();
	return 0;
}

LRESULT COutputView::OnSelectAll(WORD, WORD, HWND, BOOL&) {
	m_Edit.SetSelAll();
	return 0;
}
