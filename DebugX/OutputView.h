#pragma once

#include "PaneBase.h"
#include "Lifetime.h"

// What happens in the target, a line each: processes and threads coming and going, modules loading, and what the
// target writes with OutputDebugString. (The Command window has the debugger's own output, and all of this too, mixed.)
class COutputView :
	public CFrameWindowImpl<COutputView, CWindow, CControlWinTraits>,
	public IPane,
	public ISessionObserver {
public:
	using BaseFrame = CFrameWindowImpl<COutputView, CWindow, CControlWinTraits>;

	BEGIN_MSG_MAP(COutputView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(AppFonts::FontsChangedMessage, OnFontsChanged)
		COMMAND_ID_HANDLER(ID_EDIT_CLEAR, OnClear)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_EDIT_SELECT_ALL, OnSelectAll)
		CHAIN_MSG_MAP(BaseFrame)
	END_MSG_MAP()

	// IPane
	HWND Create(HWND hParent) override;
	void OnFinalMessage(HWND) override;

	// ISessionObserver
	void OnLogLine(const std::wstring& line) override;

private:
	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFontsChanged(UINT, WPARAM wParam, LPARAM, BOOL&);
	LRESULT OnClear(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSelectAll(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	CEdit m_Edit;
};
