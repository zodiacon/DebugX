#pragma once

#include "Debugger.h"
#include "GenericEditCommands.h"
#include "Lifetime.h"
#include "PaneBase.h"
#include "ScintillaCtrl.h"

class CCommandView :
	public CFrameWindowImpl<CCommandView, CWindow, CControlWinTraits>,
	public CGenericEditCommands<CCommandView>,
	public IPane,
	public ISessionObserver {
public:
	using BaseFrame = CFrameWindowImpl<CCommandView, CWindow, CControlWinTraits>;

	CCommandView();

	HWND Create(HWND hParent) override;

	void OnSessionChanged() override;
	void OnOutput(const DebugEngine::OutputData& data) override;

	enum { TextId = 123, ExecuteId };
	// a Scintilla style number for DML links; style 0 (plain text) and StylesCommon::Default (the
	// template StyleClearAll copies into every other style, including 0) carry the theme colors
	enum { StyleLink = 1 };

	BEGIN_MSG_MAP(CCommandView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(AppFonts::FontsChangedMessage, OnFontsChanged)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WTLHelper::ThemeChangedMessage, OnThemeChanged)
		COMMAND_HANDLER(ExecuteId, BN_CLICKED, OnExecuteCommand)
		NOTIFY_HANDLER(TextId, SCN_HOTSPOTCLICK, OnHotspotClick)
		CHAIN_MSG_MAP_ALT(CGenericEditCommands<CCommandView>, 1)
		CHAIN_MSG_MAP(BaseFrame)
	ALT_MSG_MAP(1)
		MESSAGE_HANDLER(WM_KEYDOWN, OnEditKeyDown)
		MESSAGE_HANDLER(WM_CHAR, OnEditChar)
	ALT_MSG_MAP(2)
		MESSAGE_HANDLER(WM_KEYDOWN, OnOutputKeyDown)
	END_MSG_MAP()

	void OnFinalMessage(HWND) override;

private:
	void DoExecuteCommand(PCWSTR = nullptr);
	DebugEngine::Task<void> RunCommand(CString command);
	void AppendDmlText(CString& dml);
	void AppendText(std::wstring_view text);
	void ScrollToEnd();
	void ApplyOutputColors();
	void SetBusy(bool busy);
	void UpdateControls();

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnThemeChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFontsChanged(UINT, WPARAM wParam, LPARAM, BOOL&);
	LRESULT OnExecuteCommand(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnEditChar(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnOutputKeyDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnEditKeyDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnHotspotClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

private:
	struct LinkRun {
		Scintilla::Position Start, End;
		CString Command;
	};

	CContainedWindowT<CEdit> m_CommandEdit;
	CContainedWindowT<CScintillaCtrl> m_Text;
	CStatic m_Prompt;
	CButton m_Execute;
	std::vector<LinkRun> m_Links;
	std::vector<CString> m_CommandHistory;
	int m_CommandHistoryIndex = 0;
	CString m_PromptText;
	bool m_IsBusy{ false };
	Lifetime m_Lifetime;
};
