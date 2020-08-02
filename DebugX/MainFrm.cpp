// MainFrm.cpp : implmentation of the CMainFrame class
//
/////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "resource.h"

#include "aboutdlg.h"
#include "View.h"
#include "MainFrm.h"
#include "CommandView.h"
#include "DebugManager.h"
#include "Debugger.h"
#include "KernelProcessesView.h"
#include "SecurityHelper.h"
#include "RunExeDlg.h"
#include "ProcessSelectDlg.h"
#include "UserThreadsView.h"

using namespace DebugEngine;

int CMainFrame::s_FrameWindowCount = 0;
const int WINDOW_MENU_POSITION = 5;

CMainFrame::CMainFrame() {
	++s_FrameWindowCount;
}

BOOL CMainFrame::PreTranslateMessage(MSG* pMsg) {
	if (CFrameWindowImpl<CMainFrame>::PreTranslateMessage(pMsg))
		return TRUE;

	return m_view.PreTranslateMessage(pMsg);
}

BOOL CMainFrame::OnIdle() {
	UpdateUI();
	UIUpdateToolBar();
	return FALSE;
}

void CMainFrame::OnFinalMessage(HWND) {
	if (--s_FrameWindowCount == 0) {
		DebugManager::Get().Destroy();
		::PostQuitMessage(0);
	}
	delete this;
}

HWND CMainFrame::InitView(IDebugXView* view) {
	auto hWnd = view->Create(m_view);
	if (hWnd) {
		int image = m_TabImages.AddIcon(view->GetIcon());
		m_view.AddPage(hWnd, view->GetHeader(), image, view);
	}
	return hWnd;
}

LRESULT CMainFrame::OnKickIdle(UINT, WPARAM, LPARAM, BOOL&) {
	_Module.GetMessageLoop()->OnIdle(0);
	return 0;
}

LRESULT CMainFrame::OnTimer(UINT, WPARAM id, LPARAM, BOOL&) {
	if (id == 1)
		UpdateUI(true);
	return 0;
}

LRESULT CMainFrame::OnForward(WORD, WORD, HWND, BOOL&) {
	auto msg = GetCurrentMessage();
	auto hPage = m_view.GetPageHWND(m_view.GetActivePage());
	if (hPage)
		::SendMessage(hPage, msg->message, msg->wParam, msg->lParam);
	return 0;
}

LRESULT CMainFrame::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

	// create command bar window
	HWND hWndCmdBar = m_CmdBar.Create(m_hWnd, rcDefault, nullptr, ATL_SIMPLE_CMDBAR_PANE_STYLE);
	m_CmdBar.m_bAlphaImages = true;
	m_CmdBar.AttachMenu(GetMenu());
	UIAddMenu(GetMenu());
	SetMenu(nullptr);
	InitCommandBar();

	CToolBarCtrl tb;
	auto hWndToolBar = tb.Create(m_hWnd, nullptr, nullptr, ATL_SIMPLE_TOOLBAR_PANE_STYLE | TBSTYLE_LIST, 0, ATL_IDW_TOOLBAR);
	tb.SetExtendedStyle(TBSTYLE_EX_MIXEDBUTTONS);
	InitToolBar(tb);

	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	AddSimpleReBarBand(hWndCmdBar);
	AddSimpleReBarBand(hWndToolBar, nullptr, TRUE);

	CreateSimpleStatusBar();

	m_hWndClient = m_view.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, WS_EX_CLIENTEDGE);

	UIAddToolBar(hWndToolBar);
	UISetCheck(ID_VIEW_TOOLBAR, 1);
	UISetCheck(ID_VIEW_STATUS_BAR, 1);

	CReBarCtrl(m_hWndToolBar).LockBands(TRUE);

	m_TabImages.Create(16, 16, ILC_COLOR | ILC_COLOR32, 16, 8);
	m_view.SetImageList(m_TabImages);

	// register object for message filtering and idle updates
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != nullptr);
	pLoop->AddMessageFilter(this);
	pLoop->AddIdleHandler(this);

	CMenuHandle menuMain = m_CmdBar.GetMenu();
	m_view.SetWindowMenu(menuMain.GetSubMenu(WINDOW_MENU_POSITION));

	if (s_FrameWindowCount == 1)
		DebugManager::Get().Init(this);

	auto pView = new CCommandView;
	InitView(pView);

	return 0;
}

LRESULT CMainFrame::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	// unregister message filtering and idle updates
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != nullptr);
	pLoop->RemoveMessageFilter(this);
	pLoop->RemoveIdleHandler(this);

	bHandled = FALSE;
	return 0;
}

LRESULT CMainFrame::OnFileExit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	PostMessage(WM_CLOSE);
	return 0;
}

LRESULT CMainFrame::OnAttachLocalKernel(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (!SecurityHelper::IsRunningElevated()) {
		int reply = AtlMessageBox(*this, L"Local kernel debugging requires admin privileges. Run elevated now?",
			IDS_TITLE, MB_ICONEXCLAMATION | MB_YESNO);
		if (reply == IDYES) {
			if (SecurityHelper::RunElevated(nullptr, true))
				PostQuitMessage(0);
		}
		return 0;
	}
	auto& dbg = DebugManager::DebuggerX();
	dbg.AttachLocalKernel();
	dbg.OutputPrompt();

	UIEnable(ID_DEBUG_ATTACHTOKERNEL, false);
	UIEnable(ID_DEBUG_ATTACHTOLOCALKERNEL, false);
	UIEnable(ID_DEBUG_ATTACHTOPROCESS, false);
	UIEnable(ID_DEBUG_RUNEXECUTABLE, false);

	return 0;
}

LRESULT CMainFrame::OnViewToolBar(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	static BOOL bVisible = TRUE;	// initially visible
	bVisible = !bVisible;
	CReBarCtrl rebar = m_hWndToolBar;
	int nBandIndex = rebar.IdToIndex(ATL_IDW_BAND_FIRST + 1);	// toolbar is 2nd added band
	rebar.ShowBand(nBandIndex, bVisible);
	UISetCheck(ID_VIEW_TOOLBAR, bVisible);
	UpdateLayout();
	return 0;
}

LRESULT CMainFrame::OnViewStatusBar(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	BOOL bVisible = !::IsWindowVisible(m_hWndStatusBar);
	::ShowWindow(m_hWndStatusBar, bVisible ? SW_SHOWNOACTIVATE : SW_HIDE);
	UISetCheck(ID_VIEW_STATUS_BAR, bVisible);
	UpdateLayout();
	return 0;
}

LRESULT CMainFrame::OnAppAbout(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CAboutDlg dlg;
	dlg.DoModal();
	return 0;
}

LRESULT CMainFrame::OnWindowClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	int nActivePage = m_view.GetActivePage();
	if (nActivePage != -1)
		m_view.RemovePage(nActivePage);
	else
		::MessageBeep((UINT)-1);

	return 0;
}

LRESULT CMainFrame::OnWindowCloseAll(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_view.RemoveAllPages();

	return 0;
}

LRESULT CMainFrame::OnWindowActivate(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	int nPage = wID - ID_WINDOW_TABFIRST;
	m_view.SetActivePage(nPage);

	return 0;
}

LRESULT CMainFrame::OnViewProcesses(WORD, WORD, HWND, BOOL&) {
	if ((DebugManager::DebuggerX().GetTargetType() & TargetType::Kernel) == TargetType::Kernel) {
		auto pView = new CKernelProcessesView;
		InitView(pView);
	}

	return 0;
}

LRESULT CMainFrame::OnViewThreads(WORD, WORD, HWND, BOOL&) {
	if ((DebugManager::DebuggerX().GetTargetType() & TargetType::User) == TargetType::User) {
		auto pView = new CUserThreadsView;
		InitView(pView);
	}

	return 0;
}

LRESULT CMainFrame::OnRunExe(WORD, WORD, HWND, BOOL&) {
	CRunExeDlg dlg;
	if (dlg.DoModal() == IDOK) {
		auto& dbg = DebugManager::DebuggerX();
		auto hr = dbg.LaunchAndAttach(dlg.GetExePath(), dlg.GetArguments(), dlg.GetWorkingDir());
		if (SUCCEEDED(hr)) {
			dbg.OutputPrompt();
		}
		else {
			AtlMessageBox(*this, L"Failed to launch process", IDS_TITLE, MB_ICONERROR);
		}
	}
	return 0;
}

LRESULT CMainFrame::OnAttachProcess(WORD, WORD, HWND, BOOL&) {
	CProcessSelectDlg dlg;
	if (dlg.DoModal() == IDOK) {
		auto& dbg = DebugManager::DebuggerX();
		CString name;
		auto pid = dlg.GetSelectedProcess(name);
		auto hr = dbg.AttachToProcess(pid);
		if (SUCCEEDED(hr)) {
			dbg.OutputPrompt();
		}
		else {
			CString text;
			text.Format(L"Failed to attach to PID %u", pid);
			AtlMessageBox(*this, pid, IDS_TITLE, MB_ICONERROR);
		}
	}
	return 0;
}

LRESULT CMainFrame::OnAlwaysOnTop(WORD, WORD id, HWND, BOOL&) {
	bool onTop = GetExStyle() & WS_EX_TOPMOST;
	SetWindowPos(onTop ? HWND_NOTOPMOST : HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
	UISetCheck(id, !onTop);

	return 0;
}

LRESULT CMainFrame::OnDebugBreak(WORD, WORD, HWND, BOOL&) {
	DebugManager::DebuggerX().Break();

	return 0;
}

LRESULT CMainFrame::OnDebugGo(WORD, WORD, HWND, BOOL&) {
	DebugManager::DebuggerX().ExecuteCommand("g");

	return 0;
}

LRESULT CMainFrame::OnDebugStop(WORD, WORD, HWND, BOOL&) {
	DebugManager::DebuggerX().Stop();

	return 0;
}

LRESULT CMainFrame::OnDebugStepOver(WORD, WORD, HWND, BOOL&) {
	DebugManager::DebuggerX().ExecuteCommand("p");
	return 0;
}

LRESULT CMainFrame::OnDebugStepInto(WORD, WORD, HWND, BOOL&) {
	DebugManager::DebuggerX().ExecuteCommand("t");
	return 0;
}

void CMainFrame::ProcessIdle() {
	if (::GetCurrentThreadId() == _Module.m_dwMainThreadID)
		_Module.GetMessageLoop()->OnIdle(0);
	else
		PostMessage(WM_KICKIDLE);
}

void CMainFrame::InitCommandBar() {
	struct {
		UINT id, icon;
		HICON hIcon = nullptr;
	} cmds[] = {
		{ ID_VIEW_REFRESH, IDI_REFRESH },
		{ ID_EDIT_FIND, IDI_FIND },
		{ ID_DEBUG_ATTACHTOPROCESS, IDI_PROCESS_ADD },
		{ ID_DEBUG_RUNEXECUTABLE, IDI_PROCESS_NEW },
		{ ID_DEBUG_ATTACHTOLOCALKERNEL, IDI_LOCAL_KERNEL },
		{ ID_DEBUG_ATTACHTOKERNEL, IDI_ATTACH_KERNEL },
		{ ID_DEBUG_GO, IDI_GO },
		{ ID_DEBUG_BREAK, IDI_PAUSE },
		{ ID_DEBUG_STOP, IDI_STOP },
		{ ID_DEBUG_STEPINTO, IDI_STEP_INTO },
		{ ID_DEBUG_STEPOVER, IDI_DEBUG_STEP_OVER },
		{ ID_DEBUG_STEPOUT, IDI_DEBUG_STEP_OUT },
		{ ID_VIEW_PROCESSES, IDI_PROCESSES },
		{ ID_VIEW_THREADS, IDI_THREADS },
		{ ID_VIEW_COMMAND, IDI_COMMAND },
		{ ID_VIEW_CALLSTACK, IDI_CALLSTACK },
		{ ID_VIEW_BREAKPOINTS, IDI_BREAKPOINTS },
		{ ID_WINDOW_CLOSE, IDI_WINDOW_CLOSE },
		{ ID_WINDOW_CLOSE_ALL, IDI_WINDOW_CLOSEALL },
	};
	for (auto& cmd : cmds) {
		m_CmdBar.AddIcon(cmd.icon ? AtlLoadIcon(cmd.icon) : cmd.hIcon, cmd.id);
	}
}

void CMainFrame::InitToolBar(CToolBarCtrl& tb) {
	CImageList tbImages;
	tbImages.Create(24, 24, ILC_COLOR32, 8, 4);
	tb.SetImageList(tbImages);

	struct {
		UINT id;
		int image;
		int style = BTNS_BUTTON;
		PCWSTR text = nullptr;
	} buttons[] = {
		{ ID_DEBUG_RUNEXECUTABLE, IDI_PROCESS_NEW },
		{ ID_DEBUG_ATTACHTOPROCESS, IDI_PROCESS_ADD },
		{ ID_DEBUG_ATTACHTOLOCALKERNEL, IDI_LOCAL_KERNEL },
		{ ID_DEBUG_ATTACHTOKERNEL, IDI_ATTACH_KERNEL },
		{ 0 },
		{ ID_DEBUG_GO, IDI_GO },
		{ ID_DEBUG_BREAK, IDI_PAUSE },
		{ ID_DEBUG_STOP, IDI_STOP },
		{ 0 },
		{ ID_DEBUG_STEPINTO, IDI_STEP_INTO },
		{ ID_DEBUG_STEPOVER, IDI_DEBUG_STEP_OVER },
		{ ID_DEBUG_STEPOUT, IDI_DEBUG_STEP_OUT },
	};
	for (auto& b : buttons) {
		if (b.id == 0)
			tb.AddSeparator(0);
		else {
			int image = tbImages.AddIcon(AtlLoadIconImage(b.image, 0, 24, 24));
			tb.AddButton(b.id, b.style, TBSTATE_ENABLED, image, b.text, 0);
		}
	}
}

void CMainFrame::UpdateUI(bool force) {
	using namespace DebugEngine;

	auto status = DebugManager::DebuggerX().GetExecutionStatusAsync();
	auto target = DebugManager::DebuggerX().GetTargetType();

	UIEnable(ID_DEBUG_GO, status == ExecutionStatus::Break && target != TargetType::LocalKernel, force);
	UIEnable(ID_DEBUG_BREAK, status == ExecutionStatus::Go, force);
	UIEnable(ID_DEBUG_STOP, status != ExecutionStatus::NoDebuggee, force);
	UIEnable(ID_DEBUG_STEPOVER, status == ExecutionStatus::Break && target != TargetType::LocalKernel, force);
	UIEnable(ID_DEBUG_STEPINTO, status == ExecutionStatus::Break && target != TargetType::LocalKernel, force);
	UIEnable(ID_DEBUG_STEPOUT, status == ExecutionStatus::Break && target != TargetType::LocalKernel, force);

	UIEnable(ID_VIEW_THREADS, status != ExecutionStatus::NoDebuggee);
	UIEnable(ID_VIEW_PROCESSES, status != ExecutionStatus::NoDebuggee);
	UIEnable(ID_VIEW_CALLSTACK, status != ExecutionStatus::NoDebuggee);
	UIEnable(ID_VIEW_BREAKPOINTS, status != ExecutionStatus::NoDebuggee);
}
