#pragma once

#include "Debugger.h"
#include "Lifetime.h"
#include "PaneBase.h"
#include "ScintillaCtrl.h"
#include "resource.h"

//
// What !analyze -v says about the target (IDebugger::Analyze): Debug > Analyze, and by itself when a dump is opened or
// the target stops on a second-chance exception (CMainFrame sends ID_DEBUG_ANALYZE). A read-only Scintilla; the text
// stays until the next analysis. Only the Windows Kits debugger's engine has the extension it needs.
//
class CAnalysisView :
	public CFrameWindowImpl<CAnalysisView, CWindow, CControlWinTraits>,
	public IPane {
public:
	using BaseFrame = CFrameWindowImpl<CAnalysisView, CWindow, CControlWinTraits>;

	CAnalysisView() : m_Text(this, 1) {
	}

	BEGIN_MSG_MAP(CAnalysisView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WTLHelper::ThemeChangedMessage, OnThemeChanged)
		COMMAND_ID_HANDLER(ID_DEBUG_ANALYZE, OnAnalyze)
		CHAIN_MSG_MAP(BaseFrame)
	ALT_MSG_MAP(1)
	END_MSG_MAP()

	HWND Create(HWND hParent) override {
		return BaseFrame::Create(hParent, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
	}
	void OnFinalMessage(HWND) override {
		delete this;
	}

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM lParam, BOOL&);
	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnThemeChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnAnalyze(WORD, WORD, HWND, BOOL&);

	DebugEngine::Task<void> Analyze();
	void SetText(const std::wstring& text);
	void ApplyColors();

	CContainedWindowT<CScintillaCtrl> m_Text;
	bool m_Running{ false };
	Lifetime m_Lifetime;
};
