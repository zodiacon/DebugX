#include "pch.h"
#include "AnalysisView.h"
#include "DebugManager.h"

using namespace DebugEngine;
using namespace Scintilla;

LRESULT CAnalysisView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_Text.Create(*this, nullptr, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
	m_Text.InitCall();
	m_Text->SetUndoCollection(false);
	m_Text->SetWrapMode(Wrap::None);
	m_Text->SetScrollWidthTracking(true);
	m_Text->SetMarginWidthN(1, 0);
	AppFonts::ApplyCodeFont(m_Text.Call());
	ApplyColors();
	SetText(L"Debug > Analyze runs !analyze -v on the target: when it is a dump, or it stopped on an exception.");
	return 0;
}

LRESULT CAnalysisView::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
	if (m_Text)
		m_Text.MoveWindow(0, 0, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	return 0;
}

LRESULT CAnalysisView::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
	if (m_Text)
		m_Text.SetFocus();
	return 0;
}

LRESULT CAnalysisView::OnThemeChanged(UINT, WPARAM, LPARAM, BOOL&) {
	ApplyColors();
	::SetWindowTheme(m_Text, WTLHelper::IsDarkMode() ? L"DarkMode_Explorer" : L"Explorer", nullptr);
	m_Text.Invalidate();
	return 0;
}

LRESULT CAnalysisView::OnFontsChanged(UINT, WPARAM wParam, LPARAM, BOOL&) {
	if (static_cast<AppFonts::Kind>(wParam) == AppFonts::Kind::Code) {
		AppFonts::ApplyCodeFont(m_Text.Call());
		ApplyColors();		// (StyleClearAll: the font to every style)
	}
	return 0;
}

void CAnalysisView::ApplyColors() {
	auto fore = static_cast<Colour>(::GetSysColor(COLOR_WINDOWTEXT));
	auto back = static_cast<Colour>(::GetSysColor(COLOR_WINDOW));
	m_Text->StyleSetFore(static_cast<int>(StylesCommon::Default), fore);
	m_Text->StyleSetBack(static_cast<int>(StylesCommon::Default), back);
	m_Text->StyleClearAll();
	m_Text->SetCaretFore(fore);
	m_Text->SetSelBack(true, static_cast<Colour>(::GetSysColor(COLOR_HIGHLIGHT)));
	m_Text->SetSelFore(true, static_cast<Colour>(::GetSysColor(COLOR_HIGHLIGHTTEXT)));
}

void CAnalysisView::SetText(const std::wstring& text) {
	auto utf8 = ToUtf8(text);
	m_Text->SetReadOnly(false);
	m_Text->SetText(utf8.c_str());
	m_Text->SetReadOnly(true);
	m_Text->GotoPos(0);
}

LRESULT CAnalysisView::OnAnalyze(WORD, WORD, HWND, BOOL&) {
	Analyze();
	return 0;
}

Task<void> CAnalysisView::Analyze() {
	auto alive = m_Lifetime.Watch();
	auto& session = DebugManager::Session();
	if (m_Running || !session.HasTarget() || session.IsRunning()) {
		::MessageBeep(MB_ICONWARNING);
		co_return;
	}
	m_Running = true;
	SetText(L"Running !analyze -v (this can take a while: symbols are loaded, and downloaded)...");
	std::wstring text;
	auto hr = co_await DebugManager::Debugger().Analyze(text);
	if (!*alive)
		co_return;
	m_Running = false;
	if (hr == E_NOTIMPL)
		text = L"!analyze is in the Windows Kits debugger's extensions (ext.dll); the engine in use (" + DebugManager::EnginePath() +
			L") has none. Install the Windows SDK's Debugging Tools for Windows: DebugX uses that engine when it is there.";
	SetText(text);
}
