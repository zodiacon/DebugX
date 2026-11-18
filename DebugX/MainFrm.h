// MainFrm.h : interface of the CMainFrame class
//
/////////////////////////////////////////////////////////////////////////////

#pragma once

#include "DebugManager.h"
#include "Lifetime.h"

class CMainFrame :
	public CFrameWindowImpl<CMainFrame>,
	public CAutoUpdateUI<CMainFrame>,
	public CMessageFilter,
	public CIdleHandler,
	public ISessionObserver {
public:
	DECLARE_FRAME_WND_CLASS(nullptr, IDR_MAINFRAME)

	BOOL PreTranslateMessage(MSG* pMsg) override;
	BOOL OnIdle() override;
	void OnFinalMessage(HWND) override;

	// ISessionObserver
	void OnSessionChanged() override;

	BEGIN_MSG_MAP(CMainFrame)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_CLOSE, OnClose)
		COMMAND_ID_HANDLER(ID_APP_EXIT, OnFileExit)
		COMMAND_ID_HANDLER(ID_FILE_OPEN, OnOpenDumpFile)
		COMMAND_ID_HANDLER(ID_DEBUG_ATTACHTOLOCALKERNEL, OnAttachLocalKernel)
		COMMAND_ID_HANDLER(ID_DEBUG_ATTACHTOKERNEL, OnAttachKernel)
		COMMAND_ID_HANDLER(ID_DEBUG_DETACH, OnDebugDetach)
		COMMAND_ID_HANDLER(ID_DEBUG_ATTACHTOPROCESS, OnAttachProcess)
		COMMAND_ID_HANDLER(ID_DEBUG_RUNEXECUTABLE, OnRunExe)
		COMMAND_ID_HANDLER(ID_DEBUG_BREAK, OnDebugBreak)
		COMMAND_ID_HANDLER(ID_DEBUG_GO, OnDebugGo)
		COMMAND_ID_HANDLER(ID_DEBUG_STOP, OnDebugStop)
		COMMAND_ID_HANDLER(ID_DEBUG_STEPINTO, OnDebugStepInto)
		COMMAND_ID_HANDLER(ID_DEBUG_STEPOVER, OnDebugStepOver)
		COMMAND_ID_HANDLER(ID_DEBUG_STEPOUT, OnDebugStepOut)
		COMMAND_ID_HANDLER(ID_DEBUG_ADDBREAKPOINT, OnDebugAddBreakpoint)
		COMMAND_ID_HANDLER(ID_VIEW_COMMAND, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_THREADS, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_PROCESSES, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_MODULES, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_OUTPUT, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_REGISTERS, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_CALLSTACK, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_BREAKPOINTS, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_MEMORY, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_DISASSEMBLY, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_TOOLBAR, OnViewToolBar)
		COMMAND_ID_HANDLER(ID_VIEW_STATUS_BAR, OnViewStatusBar)
		COMMAND_ID_HANDLER(ID_WINDOW_WINDOWS, OnWindowWindows)
		COMMAND_ID_HANDLER(ID_WINDOW_CLOSE, OnWindowClose)
		COMMAND_ID_HANDLER(ID_WINDOW_CLOSE_ALL, OnWindowCloseAll)
		COMMAND_ID_HANDLER(ID_WINDOW_RESETLAYOUT, OnWindowResetLayout)
		COMMAND_ID_HANDLER(ID_OPTIONS_ALWAYSONTOP, OnAlwaysOnTop)
		COMMAND_ID_HANDLER(ID_OPTIONS_DARKMODE, OnToggleDarkMode)
		COMMAND_ID_HANDLER(ID_OPTIONS_DEBUGGERMARKUPLANGUAGE, OnToggleDml)
		COMMAND_ID_HANDLER(ID_APP_ABOUT, OnAppAbout)
		CHAIN_MSG_MAP(CAutoUpdateUI<CMainFrame>)
		CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
		COMMAND_RANGE_HANDLER(0x8000, 0xffff, OnForward)
		REFLECT_NOTIFICATIONS()
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	void InitMenu(HMENU menu);
	void InitDock();
	void ApplyTheme();
	void SaveLayout();
	void UpdateUI(bool force = false);

	DebugEngine::Task<void> RunExecutable(CString exePath, CString arguments, CString workingDir);
	DebugEngine::Task<void> AttachToProcess(DWORD pid);
	DebugEngine::Task<void> AttachLocalKernel();
	DebugEngine::Task<void> AttachKernel(CString connection);
	DebugEngine::Task<void> OpenDumpFile(CString path);
	DebugEngine::Task<void> AddBreakpoint(CString expression);
	// nothing is being debugged, and nothing is on its way to being
	bool CanStartSession() const;

private:
	LRESULT OnForward(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled);
	LRESULT OnClose(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled);
	LRESULT OnFileExit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnAttachLocalKernel(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnAttachKernel(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnOpenDumpFile(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDebugDetach(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnToggleDml(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnViewToolBar(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnViewStatusBar(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnAppAbout(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnViewPane(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnWindowWindows(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnWindowClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnWindowCloseAll(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnWindowResetLayout(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnRunExe(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnAttachProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnAlwaysOnTop(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnToggleDarkMode(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDebugBreak(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDebugGo(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDebugStop(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDebugStepOver(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDebugStepInto(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDebugStepOut(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDebugAddBreakpoint(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

private:
	WTLDock::CDockHost m_Dock;
	Lifetime m_Lifetime;
};
