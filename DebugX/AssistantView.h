#pragma once

#include "GenericEditCommands.h"
#include "Lifetime.h"
#include "McpHost.h"
#include "PaneBase.h"
#include "ThemedBackground.h"
#include "..\DebugX.Mcp\Assistant.h"
#include "..\DebugX.Mcp\Transcript.h"
#include "..\wtlhelper\MarkdownView\include\MarkdownView.h"

//
// The Assistant pane: a conversation with Claude about the target (DebugX::Ai::Assistant, DebugX.Mcp), which looks into it
// with the MCP server's tools. The conversation is a Markdown document (DebugX::Ai::Transcript) in a Markdown view
// (WTLHelper's CMarkdownViewCtrl): the user's messages, the answers as they stream, summaries of the model's reasoning, each
// tool call as a <details> that opens on its whole result; the symbols and addresses in the answers are links to the
// Disassembly pane (dbgx: links, MDVN_LINKCLICK). While an answer streams the document is set again at most every
// RenderDelay ms (the view parses it all again: its selection and opened <details> go back to how they were); it follows the
// end while the end is showing. An edit below takes the next message (Enter sends, Shift+Enter is a new line). A tool that
// changes the session (run, step, breakpoints, a command) is asked about first, unless the settings say not to. Settings:
// the model, its effort, the API key (CAssistantSettingsDlg).
//
class CAssistantView :
	public CFrameWindowImpl<CAssistantView, CWindow, CControlWinTraits>,
	public CGenericEditCommands<CAssistantView>,
	public CThemedBackground<CAssistantView>,
	public IPane {
public:
	using BaseFrame = CFrameWindowImpl<CAssistantView, CWindow, CControlWinTraits>;

	CAssistantView();

	HWND Create(HWND hParent) override;
	void OnFinalMessage(HWND) override {
		delete this;
	}

	enum { TextId = 200, InputId, SendId, NewId, SettingsId };
	enum : UINT_PTR { RenderTimerId = 0x7A10 };
	static constexpr UINT RenderDelay = 120;

	BEGIN_MSG_MAP(CAssistantView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(AppFonts::FontsChangedMessage, OnFontsChanged)
		COMMAND_ID_HANDLER(SendId, OnSend)
		COMMAND_ID_HANDLER(NewId, OnNewConversation)
		COMMAND_ID_HANDLER(SettingsId, OnSettings)
		NOTIFY_HANDLER(TextId, MDVN_LINKCLICK, OnLinkClick)
		NOTIFY_HANDLER(TextId, MDVN_CONTEXTMENU, OnViewContextMenu)
		CHAIN_MSG_MAP_ALT(CGenericEditCommands<CAssistantView>, 1)
		CHAIN_MSG_MAP(CThemedBackground<CAssistantView>)
		CHAIN_MSG_MAP(BaseFrame)
	ALT_MSG_MAP(1)
		MESSAGE_HANDLER(WM_KEYDOWN, OnInputKeyDown)
		MESSAGE_HANDLER(WM_CHAR, OnInputChar)
	END_MSG_MAP()

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled);
	LRESULT OnSize(UINT, WPARAM, LPARAM lParam, BOOL&);
	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTimer(UINT, WPARAM wParam, LPARAM, BOOL& handled);
	LRESULT OnFontsChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSend(WORD, WORD, HWND, BOOL&);
	LRESULT OnNewConversation(WORD, WORD, HWND, BOOL&);
	LRESULT OnSettings(WORD, WORD, HWND, BOOL&);
	LRESULT OnLinkClick(int, LPNMHDR hdr, BOOL&);
	LRESULT OnViewContextMenu(int, LPNMHDR hdr, BOOL&);
	LRESULT OnInputKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& handled);
	LRESULT OnInputChar(UINT, WPARAM wParam, LPARAM, BOOL& handled);

	void Send();
	void NewConversation();
	// the settings dialog; true if OK
	bool EditSettings();
	// the model, effort and API key as the settings have them now, to the Assistant
	void ApplySettings();
	void OnEvent(const DebugX::Ai::AssistantEvent& e);
	DebugX::Ai::Approval Approve(const std::wstring& tool, const std::wstring& arguments);

	// the transcript into the view: now, or in a moment (a stream of pieces)
	void Render();
	void RenderSoon();
	// the view's fonts: the UI font for the text, the code font for code (AppFonts)
	void ApplyTextStyle();
	void UpdateControls();
	void UpdateStatus();
	void ShowWelcome();

	CMarkdownViewCtrl m_View;
	CContainedWindowT<CEdit> m_Input;
	CButton m_Send, m_New, m_Settings;
	CStatic m_Status;
	McpHost m_Host{ false };
	std::unique_ptr<DebugX::Ai::Assistant> m_Assistant;
	DebugX::Ai::Transcript m_Transcript;
	std::wstring m_Key;		// the API key the Assistant's transport has
	bool m_Busy{ false };
	bool m_RenderPending{ false };
	bool m_FollowNext{ false };		// the next render goes to the end, wherever the view was
	DebugX::Ai::AssistantEvent m_Usage{ DebugX::Ai::AssistantEvent::Kind::Usage };
	Lifetime m_Lifetime;
};
