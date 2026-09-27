#pragma once

#include "Debugger.h"
#include "GenericEditCommands.h"
#include "Lifetime.h"
#include "PaneBase.h"

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

	BEGIN_MSG_MAP(CCommandView)
		MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WTLHelper::ThemeChangedMessage, OnThemeChanged)
		COMMAND_HANDLER(ExecuteId, BN_CLICKED, OnExecuteCommand)
		NOTIFY_HANDLER(TextId, EN_MSGFILTER, OnMessageFilter)
		CHAIN_MSG_MAP_ALT(CGenericEditCommands<CCommandView>, 1)
		CHAIN_MSG_MAP(BaseFrame)
	ALT_MSG_MAP(1)
		MESSAGE_HANDLER(WM_KEYDOWN, OnEditKeyDown)
		MESSAGE_HANDLER(WM_CHAR, OnEditChar)
	ALT_MSG_MAP(2)
		MESSAGE_HANDLER(WM_SETCURSOR, OnRichEditSetCursor)
		MESSAGE_HANDLER(WM_KEYDOWN, OnRichEditKeyDown)
	END_MSG_MAP()

	void OnFinalMessage(HWND) override;

private:
	void DoExecuteCommand(PCWSTR = nullptr);
	DebugEngine::Task<void> RunCommand(CString command);
	void AddDmlText(CString& dml);
	void SetBusy(bool busy);
	void UpdateControls();

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSetCursor(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnThemeChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnExecuteCommand(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnEditChar(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnRichEditKeyDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnEditKeyDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnMessageFilter(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnRichEditSetCursor(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

private:
	CContainedWindowT<CEdit> m_CommandEdit;
	CContainedWindowT<CRichEditCtrl> m_Text;
	CStatic m_Prompt;
	CButton m_Execute;
	std::unordered_map<int, CString> m_Links;
	std::vector<CString> m_CommandHistory;
	int m_CommandHistoryIndex = 0;
	CString m_PromptText;
	HCURSOR m_hCursor{ nullptr };
	bool m_IsBusy{ false };
	Lifetime m_Lifetime;
};

