#pragma once

#include "GenericEditCommands.h"
#include "Lifetime.h"
#include "McpHost.h"
#include "PaneBase.h"
#include "ScintillaCtrl.h"
#include "ThemedBackground.h"
#include "..\DebugX.Mcp\Assistant.h"

//
// The Assistant pane: a conversation with Claude about the target (DebugX::Ai::Assistant, DebugX.Mcp), which looks into it
// with the MCP server's tools. A read-only Scintilla shows the conversation as it streams -- the user's messages, the
// answers, summaries of the model's reasoning, each tool call with the start of its result -- and makes the symbols and
// addresses in it links to the Disassembly pane; an edit below takes the next message (Enter sends, Shift+Enter is a new
// line). A tool that changes the session (run, step, breakpoints, a command) is asked about first, unless the settings
// say not to. Settings: the model, its effort, the API key (CAssistantSettingsDlg).
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
	// the transcript's styles (0 is the answers' text, which StyleClearAll gives every style first)
	enum {
		StyleText = 0, StyleUser, StyleThinking, StyleToolCall, StyleToolResult, StyleError, StyleInfo, StyleLink,
	};

	BEGIN_MSG_MAP(CAssistantView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WTLHelper::ThemeChangedMessage, OnThemeChanged)
		MESSAGE_HANDLER(AppFonts::FontsChangedMessage, OnFontsChanged)
		COMMAND_ID_HANDLER(SendId, OnSend)
		COMMAND_ID_HANDLER(NewId, OnNewConversation)
		COMMAND_ID_HANDLER(SettingsId, OnSettings)
		NOTIFY_HANDLER(TextId, SCN_HOTSPOTCLICK, OnHotspotClick)
		CHAIN_MSG_MAP_ALT(CGenericEditCommands<CAssistantView>, 1)
		CHAIN_MSG_MAP(CThemedBackground<CAssistantView>)
		CHAIN_MSG_MAP(BaseFrame)
	ALT_MSG_MAP(1)
		MESSAGE_HANDLER(WM_KEYDOWN, OnInputKeyDown)
		MESSAGE_HANDLER(WM_CHAR, OnInputChar)
	ALT_MSG_MAP(2)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnTextContextMenu)
	END_MSG_MAP()

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled);
	LRESULT OnSize(UINT, WPARAM, LPARAM lParam, BOOL&);
	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnThemeChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnFontsChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSend(WORD, WORD, HWND, BOOL&);
	LRESULT OnNewConversation(WORD, WORD, HWND, BOOL&);
	LRESULT OnSettings(WORD, WORD, HWND, BOOL&);
	LRESULT OnHotspotClick(int, LPNMHDR hdr, BOOL&);
	LRESULT OnInputKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& handled);
	LRESULT OnInputChar(UINT, WPARAM wParam, LPARAM, BOOL& handled);
	LRESULT OnTextContextMenu(UINT, WPARAM, LPARAM lParam, BOOL&);

	void Send();
	void NewConversation();
	// the settings dialog; true if OK
	bool EditSettings();
	// the model, effort and API key as the settings have them now, to the Assistant
	void ApplySettings();
	void OnEvent(const DebugX::Ai::AssistantEvent& e);
	DebugX::Ai::Approval Approve(const std::wstring& tool, const std::wstring& arguments);

	// text at the end of the transcript, in a style; it follows the end if the end was showing
	void Append(std::wstring_view text, int style);
	void AppendLine(std::wstring_view text, int style);
	// the transcript ends with a line break (a block starts on a line of its own)
	void EndLine();
	// the symbols and addresses in the text from 'start' on: links to the Disassembly pane
	void MarkLinks(Scintilla::Position start);
	void ApplyStyles();
	void UpdateControls();
	void UpdateStatus();
	void ShowWelcome();

	struct Link {
		Scintilla::Position Start, End;
		std::wstring Expression;
	};

	CContainedWindowT<CScintillaCtrl> m_Text;
	CContainedWindowT<CEdit> m_Input;
	CButton m_Send, m_New, m_Settings;
	CStatic m_Status;
	McpHost m_Host{ false };
	std::unique_ptr<DebugX::Ai::Assistant> m_Assistant;
	std::wstring m_Key;		// the API key the Assistant's transport has
	std::vector<Link> m_Links;
	Scintilla::Position m_BlockStart{ 0 };
	bool m_Busy{ false };
	DebugX::Ai::AssistantEvent m_Usage{ DebugX::Ai::AssistantEvent::Kind::Usage };
	Lifetime m_Lifetime;
};
