#pragma once

#include "..\DebugEngine\Debugger.h"

class CCommandView : public CWindowImpl<CCommandView> {
public:
	CCommandView();

	BEGIN_MSG_MAP(CCommandView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER((UINT)DebugEngine::DebuggerNotificationType::Output, OnDebugOutput)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		COMMAND_CODE_HANDLER(BN_CLICKED, OnExecuteCommand)
	ALT_MSG_MAP(1)
		MESSAGE_HANDLER(WM_CHAR, OnEditChar)
	ALT_MSG_MAP(2)
		MESSAGE_HANDLER(WM_KEYDOWN, OnRichEditKeyDown)
	END_MSG_MAP()

	void OnFinalMessage(HWND) override;

private:
	void DoExecuteCommand();

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDebugOutput(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnExecuteCommand(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnEditChar(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnRichEditKeyDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

private:
	CContainedWindowT<CEdit> m_CommandEdit;
	CContainedWindowT<CRichEditCtrl> m_Text;
	CStatic m_Prompt;
	CButton m_Execute;
	std::vector<CString> m_CommandHistory;
};

