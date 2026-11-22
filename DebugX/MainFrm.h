// MainFrm.h : interface of the CMainFrame class
//
/////////////////////////////////////////////////////////////////////////////

#pragma once

#include <optional>
#include "DebugManager.h"
#include "Lifetime.h"
#include "SourceDocuments.h"
#include "FindDlg.h"
#include "AppMessages.h"
#include "DefaultLayouts.h"

class CMainFrame :
	public CFrameWindowImpl<CMainFrame>,
	public CAutoUpdateUI<CMainFrame>,
	public CMessageFilter,
	public CIdleHandler,
	public ISessionObserver {
public:
	DECLARE_FRAME_WND_CLASS(nullptr, IDR_MAINFRAME)

	// The panes, and the arrangement the last run left (which can show the frame: WTLDock restores its placement too).
	// Called once the frame is created, not from WM_CREATE: WTLHelper's dark-mode hook gives a top-level window its
	// menu bar subclass when WM_CREATE returns, and a frame shown before that paints a light menu bar that stays.
	void InitDock();

	BOOL PreTranslateMessage(MSG* pMsg) override;
	BOOL OnIdle() override;
	void OnFinalMessage(HWND) override;

	// ISessionObserver
	void OnSessionChanged() override;
	void OnBreakpointsChanged() override;
	void OnTargetEnded() override;
	// a target of the other kind (user / kernel mode) brings that mode's window layout
	void OnTargetAttached() override;
	// a second-chance exception is analyzed when the target has stopped for it
	void OnException(const DebugEngine::ExceptionData& data) override;
	void OnBroke() override;

	BEGIN_MSG_MAP(CMainFrame)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_CLOSE, OnClose)
		MESSAGE_HANDLER(WM_INITMENUPOPUP, OnInitMenuPopup)
		MESSAGE_HANDLER(WM_ADDWATCH, OnAddWatch)
		MESSAGE_HANDLER(WM_GOTODISASSEMBLY, OnGoToDisassembly)
		NOTIFY_CODE_HANDLER(TTN_GETDISPINFOW, OnToolTipText)
		COMMAND_RANGE_HANDLER(ID_FILE_RECENT1, ID_FILE_RECENT10, OnRecentTarget)
		COMMAND_ID_HANDLER(ID_FILE_CLEARRECENT, OnClearRecent)
		COMMAND_ID_HANDLER(ID_DEBUG_SAVEDUMP, OnSaveDump)
		COMMAND_ID_HANDLER(ID_DEBUG_ANALYZE, OnDebugAnalyze)
		COMMAND_ID_HANDLER(ID_DEBUG_CONNECTSERVER, OnConnectServer)
		COMMAND_ID_HANDLER(ID_DEBUG_DISCONNECTSERVER, OnDisconnectServer)
		COMMAND_ID_HANDLER(ID_DEBUG_RUNSCRIPT, OnScript)
		COMMAND_ID_HANDLER(ID_DEBUG_LOADSCRIPT, OnScript)
		COMMAND_ID_HANDLER(ID_DEBUG_UNLOADSCRIPT, OnScript)
		COMMAND_ID_HANDLER(ID_VIEW_ANALYSIS, OnViewPane)
		COMMAND_ID_HANDLER(ID_APP_EXIT, OnFileExit)
		COMMAND_ID_HANDLER(ID_FILE_RUNASADMINISTRATOR, OnRunAsAdministrator)
		COMMAND_ID_HANDLER(ID_FILE_OPEN, OnOpenDumpFile)
		COMMAND_ID_HANDLER(ID_DEBUG_ATTACHTOLOCALKERNEL, OnAttachLocalKernel)
		COMMAND_ID_HANDLER(ID_DEBUG_ATTACHTOKERNEL, OnAttachKernel)
		COMMAND_ID_HANDLER(ID_DEBUG_DETACH, OnDebugDetach)
		COMMAND_ID_HANDLER(ID_DEBUG_ATTACHTOPROCESS, OnAttachProcess)
		COMMAND_ID_HANDLER(ID_DEBUG_RUNEXECUTABLE, OnRunExe)
		COMMAND_ID_HANDLER(ID_DEBUG_BREAK, OnDebugBreak)
		COMMAND_ID_HANDLER(ID_DEBUG_GO, OnDebugGo)
		COMMAND_ID_HANDLER(ID_DEBUG_STOP, OnDebugStop)
		COMMAND_ID_HANDLER(ID_DEBUG_RESTART, OnDebugRestart)
		COMMAND_ID_HANDLER(ID_DEBUG_STEPINTO, OnDebugStepInto)
		COMMAND_ID_HANDLER(ID_DEBUG_STEPOVER, OnDebugStepOver)
		COMMAND_ID_HANDLER(ID_DEBUG_STEPOUT, OnDebugStepOut)
		COMMAND_ID_HANDLER(ID_DEBUG_ADDBREAKPOINT, OnDebugAddBreakpoint)
		COMMAND_ID_HANDLER(ID_DEBUG_SHOWNEXT, OnDebugShowNext)
		COMMAND_ID_HANDLER(ID_DEBUG_DELETEALLBREAKPOINTS, OnDeleteAllBreakpoints)
		COMMAND_ID_HANDLER(ID_DEBUG_ENABLEALLBREAKPOINTS, OnEnableAllBreakpoints)
		COMMAND_ID_HANDLER(ID_DEBUG_DISABLEALLBREAKPOINTS, OnEnableAllBreakpoints)
		COMMAND_ID_HANDLER(ID_DEBUG_EXCEPTIONS, OnDebugExceptions)
		COMMAND_ID_HANDLER(ID_DEBUG_RELOADSYMBOLS, OnReloadSymbols)
		COMMAND_ID_HANDLER(ID_FILE_OPENSOURCE, OnOpenSourceFile)
		COMMAND_ID_HANDLER(ID_OPTIONS_SYMBOLPATHS, OnSymbolPaths)
		COMMAND_ID_HANDLER(ID_EDIT_FIND, OnEditFind)
		COMMAND_ID_HANDLER(ID_EDIT_FINDNEXT, OnEditFindAgain)
		COMMAND_ID_HANDLER(ID_EDIT_FINDPREV, OnEditFindAgain)
		COMMAND_ID_HANDLER(ID_VIEW_COMMAND, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_THREADS, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_PROCESSES, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_KERNELTHREADS, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_USERPROCESSES, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_HANDLES, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_DRIVERS, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_OBJECTS, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_IRPS, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_MEMORY2, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_MEMORY3, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_MEMORY4, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_MODULES, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_OUTPUT, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_REGISTERS, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_CALLSTACK, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_BREAKPOINTS, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_MEMORY, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_DISASSEMBLY, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_LOCALS, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_WATCH, OnViewPane)
		COMMAND_ID_HANDLER(ID_VIEW_TOOLBAR, OnViewToolBar)
		COMMAND_ID_HANDLER(ID_VIEW_STATUS_BAR, OnViewStatusBar)
		COMMAND_ID_HANDLER(ID_WINDOW_WINDOWS, OnWindowWindows)
		COMMAND_ID_HANDLER(ID_WINDOW_CLOSE, OnWindowClose)
		COMMAND_ID_HANDLER(ID_WINDOW_CLOSE_ALL, OnWindowCloseAll)
		COMMAND_ID_HANDLER(ID_WINDOW_RESETLAYOUT, OnWindowResetLayout)
		COMMAND_ID_HANDLER(ID_WINDOW_LAYOUT_USERDEFAULT, OnWindowDefaultLayout)
		COMMAND_ID_HANDLER(ID_WINDOW_LAYOUT_KERNELDEFAULT, OnWindowDefaultLayout)
		COMMAND_ID_HANDLER(ID_WINDOW_SAVELAYOUT, OnWindowSaveLayout)
		COMMAND_RANGE_HANDLER(ID_WINDOW_LAYOUT1, ID_WINDOW_LAYOUT20, OnWindowApplyLayout)
		COMMAND_RANGE_HANDLER(ID_WINDOW_DELETELAYOUT1, ID_WINDOW_DELETELAYOUT20, OnWindowDeleteLayout)
		COMMAND_ID_HANDLER(ID_OPTIONS_ALWAYSONTOP, OnAlwaysOnTop)
		COMMAND_ID_HANDLER(ID_OPTIONS_DARKMODE, OnToggleDarkMode)
		COMMAND_ID_HANDLER(ID_OPTIONS_CODEFONT, OnChooseFont)
		COMMAND_ID_HANDLER(ID_OPTIONS_UIFONT, OnChooseFont)
		COMMAND_ID_HANDLER(ID_OPTIONS_DEBUGGERMARKUPLANGUAGE, OnToggleDml)
		COMMAND_ID_HANDLER(ID_OPTIONS_DEBUGCHILDPROCESSES, OnToggleChildProcesses)
		COMMAND_ID_HANDLER(ID_OPTIONS_HEXDISPLAY, OnToggleHexDisplay)
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
	// saves what a close saves, starts DebugX again elevated and closes this one; false if it was not started (UAC declined)
	bool RelaunchElevated();
	// Gives up a kernel connection that waits for its target, by starting DebugX again (asked first): no interrupt ends
	// DbgEng's wait for a kernel to connect, nor does EndSession from another thread, and there is one engine per process.
	void CancelKernelConnection();
	void SaveState();
	void ApplyTheme();
	void SaveLayout();
	// Window layouts. The one in use is the layout of m_LayoutMode; the other mode's waits in m_ModeLayouts.
	bool ApplyDefaultLayout(LayoutMode mode, bool report);
	void SwitchLayoutMode(LayoutMode mode);
	void SaveNamedLayouts();
	void FillLayoutsMenu(CMenuHandle menu);
	LRESULT OnWindowDefaultLayout(WORD, WORD id, HWND, BOOL&);
	LRESULT OnWindowSaveLayout(WORD, WORD, HWND, BOOL&);
	LRESULT OnWindowApplyLayout(WORD, WORD id, HWND, BOOL&);
	LRESULT OnWindowDeleteLayout(WORD, WORD id, HWND, BOOL&);
	void UpdateUI(bool force = false);
	// the window's title: the target and whether it runs, then DebugX's own (m_BaseTitle)
	void SetTargetName(CString name);
	void UpdateTitle();

	DebugEngine::Task<void> RunExecutable(CString exePath, CString arguments, CString workingDir);
	// (by the process name too, if it is known: what Recent Targets keeps)
	DebugEngine::Task<void> AttachToProcess(DWORD pid, CString name = {});
	DebugEngine::Task<void> SaveDump();
	DebugEngine::Task<void> AttachLocalKernel();
	DebugEngine::Task<void> AttachKernel(CString connection);
	DebugEngine::Task<void> OpenDumpFile(CString path);
	// the executable of the session ended and launched again, with the breakpoints it had
	DebugEngine::Task<void> Restart();
	// SavedBreakpoints: the program's, set again in the new session (it is the program of m_BreakpointsKey after);
	// the number that could not be set
	DebugEngine::Task<int> RestoreBreakpoints(std::wstring key);
	// the breakpoints of the program being debugged, as they are now, are saved
	DebugEngine::Task<void> SaveBreakpoints();
	void ReportRestoreFailures(int failed);
	DebugEngine::Task<void> AddBreakpoint(DebugEngine::BreakpointSettings settings);
	DebugEngine::Task<void> EditExceptionSettings();
	// the Exception Settings the user saved, applied to the engine (at startup)
	DebugEngine::Task<void> ApplySavedEventFilters();
	// nothing is being debugged, and nothing is on its way to being
	bool CanStartSession() const;
	DebugEngine::Task<void> EditSymbolPaths();
	// the symbol and source paths the user saved, given to the engine (at startup)
	void ApplySavedSymbolPaths();
	// F10 / F11 / Shift+F11: by source line, unless the Disassembly window is the one in use
	void Step(DebugEngine::StepKind kind);
	// the id of the pane that has (or last had) the focus; empty if none
	std::wstring ActivePaneId() const;
	// the text control (Scintilla) of the active pane, which Find searches; null if it has none
	HWND ActiveTextControl() const;
	void FindNext(bool up);

private:
	LRESULT OnForward(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled);
	LRESULT OnClose(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled);
	LRESULT OnFileExit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnRunAsAdministrator(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
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
	LRESULT OnChooseFont(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDebugBreak(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDebugGo(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDebugStop(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDebugRestart(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDebugStepOver(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDebugStepInto(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDebugStepOut(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDebugAddBreakpoint(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	// Show Next Statement: the thread's frame again, and its line in the source (with the focus), else its instruction
	// in the Disassembly window (also when that is the window in use)
	LRESULT OnDebugShowNext(WORD, WORD, HWND, BOOL&);
	DebugEngine::Task<void> ShowNextStatement();
	LRESULT OnDeleteAllBreakpoints(WORD, WORD, HWND, BOOL&);
	// (and Disable All)
	LRESULT OnEnableAllBreakpoints(WORD, WORD id, HWND, BOOL&);
	LRESULT OnDebugExceptions(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnReloadSymbols(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnOpenSourceFile(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSymbolPaths(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnInitMenuPopup(UINT, WPARAM wParam, LPARAM, BOOL& bHandled);
	LRESULT OnAddWatch(UINT, WPARAM, LPARAM lParam, BOOL&);
	// shows the Disassembly pane and has it go to the address (WM_GOTODISASSEMBLY, from a pane)
	LRESULT OnGoToDisassembly(UINT, WPARAM, LPARAM lParam, BOOL&);
	// the toolbar buttons' tips: the text of their menu items
	LRESULT OnToolTipText(int, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnRecentTarget(WORD, WORD id, HWND, BOOL&);
	LRESULT OnClearRecent(WORD, WORD, HWND, BOOL&);
	LRESULT OnSaveDump(WORD, WORD, HWND, BOOL&);
	// shows the Analysis pane and has it run !analyze -v
	LRESULT OnDebugAnalyze(WORD, WORD, HWND, BOOL&);
	// a process server (dbgsrv) on another machine: Attach to Process and Run Executable are about its processes then
	LRESULT OnConnectServer(WORD, WORD, HWND, BOOL&);
	LRESULT OnDisconnectServer(WORD, WORD, HWND, BOOL&);
	DebugEngine::Task<void> ConnectServer(CString options);
	DebugEngine::Task<void> DisconnectServer();
	DebugEngine::Task<void> AttachOnServer();
	// debugger scripts (JavaScript): Run Script, Load Script (a file to pick), Unload Script (one of those loaded)
	LRESULT OnScript(WORD, WORD id, HWND, BOOL&);
	DebugEngine::Task<void> RunScript(DebugEngine::IDebugger::ScriptAction action, std::wstring path);
	DebugEngine::Task<void> UnloadScript();
	LRESULT OnToggleChildProcesses(WORD, WORD id, HWND, BOOL&);
	// Options > Hexadecimal Display: integers in Locals, Watch and the call tips (SetDisplayRadix)
	LRESULT OnToggleHexDisplay(WORD, WORD id, HWND, BOOL&);
	LRESULT OnEditFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnEditFindAgain(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

private:
	WTLDock::CDockHost m_Dock;
	SourceDocuments m_Sources;
	// how the target being debugged was launched (Run Executable); none: it was attached to or opened
	struct Launch {
		CString ExePath, Arguments, WorkingDir;
	};
	std::optional<Launch> m_Launch;
	FindOptions m_Find;
	bool m_Restarting{ false };
	// Attach to Kernel is waiting for the target to connect (Break and Stop give up waiting: CancelKernelConnection)
	bool m_Connecting{ false };
	// what is being debugged, for the title (set by each start: the program's or dump's file name, ...); empty: nothing
	CString m_TargetName;
	// the title without a target ("DebugX", with " (Administrator)" when elevated)
	CString m_BaseTitle;
	// the program whose breakpoints are kept (SavedBreakpoints::KeyOf): one that was run or attached to; empty: none
	std::wstring m_BreakpointsKey;
	bool m_AnalyzeOnBreak{ false };
	// the process server connected to (its options); empty: none
	CString m_ProcessServer;
	// the kind of target the window layout in use is for, and the last layout of the other kind (by LayoutModeName)
	LayoutMode m_LayoutMode{ LayoutMode::User };
	WTLDock::DockLayoutStore m_ModeLayouts;
	Lifetime m_Lifetime;
};
