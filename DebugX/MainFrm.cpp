// MainFrm.cpp : implmentation of the CMainFrame class
//
/////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "resource.h"

#include "aboutdlg.h"
#include "MainFrm.h"
#include "DebugManager.h"
#include "Debugger.h"
#include "SecurityHelper.h"
#include "ToolbarHelper.h"
#include "AppSettings.h"
#include "RunExeDlg.h"
#include "ProcessSelectDlg.h"
#include "PaneRegistry.h"

using namespace DebugEngine;
using namespace WTLDock;

namespace {
	// where the arrangement of the tool windows and the frame is kept between runs
	std::wstring GetLayoutPath() {
		wil::unique_cotaskmem_string folder;
		if (FAILED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &folder)))
			return {};

		std::wstring dir(folder.get());
		dir += L"\\DebugX";
		::CreateDirectory(dir.c_str(), nullptr);
		return dir + L"\\layout.json";
	}
}

BOOL CMainFrame::PreTranslateMessage(MSG* pMsg) {
	if (m_Dock.PreTranslateMessage(pMsg))
		return TRUE;

	return CFrameWindowImpl<CMainFrame>::PreTranslateMessage(pMsg);
}

BOOL CMainFrame::OnIdle() {
	UIUpdateToolBar();

	return FALSE;
}

void CMainFrame::OnFinalMessage(HWND) {
	DebugManager::Get().Destroy();
	::PostQuitMessage(0);
	delete this;
}

void CMainFrame::OnSessionChanged() {
	UpdateUI(true);
}

LRESULT CMainFrame::OnForward(WORD, WORD, HWND, BOOL&) {
	auto msg = GetCurrentMessage();
	auto pane = m_Dock.ActivePane();
	if (pane && pane->hWnd)
		::SendMessage(pane->hWnd, msg->message, msg->wParam, msg->lParam);
	return 0;
}

LRESULT CMainFrame::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	if (FAILED(DebugManager::Get().Init())) {
		AtlMessageBox(nullptr, L"Failed to initialize the debugger engine", IDS_TITLE, MB_ICONERROR);
		return -1;
	}

	::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

	UIAddMenu(GetMenu());

	ToolBarButtonInfo buttons[] = {
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
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	// the frame is per-monitor DPI aware: size the images for the monitor it is on
	auto tb = ToolbarHelper::CreateAndInitToolBar(m_hWnd, buttons, _countof(buttons), ::MulDiv(24, ::GetDpiForWindow(m_hWnd), 96));
	UIAddToolBar(tb);
	AddSimpleReBarBand(tb);
	CreateSimpleStatusBar();

	m_hWndClient = m_Dock.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);

	UISetCheck(ID_VIEW_TOOLBAR, 1);
	UISetCheck(ID_VIEW_STATUS_BAR, 1);

	// register object for message filtering and idle updates
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != nullptr);
	pLoop->AddMessageFilter(this);
	pLoop->AddIdleHandler(this);

	DebugManager::Session().AddObserver(this);

	ApplyTheme();
	InitDock();
	UpdateUI(true);

	return 0;
}

LRESULT CMainFrame::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	DebugManager::Session().RemoveObserver(this);
	AppSettings::Get().Save();

	// unregister message filtering and idle updates
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != nullptr);
	pLoop->RemoveMessageFilter(this);
	pLoop->RemoveIdleHandler(this);

	bHandled = FALSE;
	return 0;
}

LRESULT CMainFrame::OnClose(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	AppSettings::Get().PaneStates(PaneRegistry::Get().SaveStates());
	SaveLayout();

	// the target is detached when the debugger is destroyed, see OnFinalMessage
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
	AttachLocalKernel();

	return 0;
}

LRESULT CMainFrame::OnViewToolBar(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	static BOOL bVisible = TRUE;	// initially visible
	bVisible = !bVisible;
	CReBarCtrl rebar = m_hWndToolBar;
	int nBandIndex = rebar.IdToIndex(ATL_IDW_BAND_FIRST);	// the toolbar is the only band
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

LRESULT CMainFrame::OnViewPane(WORD, WORD id, HWND, BOOL&) {
	if (auto info = PaneRegistry::Get().FindByCommand(id)) {
		if (auto pane = m_Dock.Layout().FindPane(info->Id))
			m_Dock.ShowPane(pane);
	}
	return 0;
}

LRESULT CMainFrame::OnWindowWindows(WORD, WORD, HWND, BOOL&) {
	m_Dock.ShowWindowsDialog(m_hWnd);
	return 0;
}

LRESULT CMainFrame::OnWindowClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto pane = m_Dock.ActivePane())
		m_Dock.ClosePane(pane);
	else
		::MessageBeep((UINT)-1);

	return 0;
}

LRESULT CMainFrame::OnWindowCloseAll(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Dock.CloseAllDocuments();

	return 0;
}

LRESULT CMainFrame::OnWindowResetLayout(WORD, WORD, HWND, BOOL&) {
	std::wstring error;
	if (!m_Dock.ResetLayout(&error))
		AtlMessageBox(*this, error.c_str(), IDS_TITLE, MB_ICONERROR);

	return 0;
}

LRESULT CMainFrame::OnRunExe(WORD, WORD, HWND, BOOL&) {
	CRunExeDlg dlg;
	if (dlg.DoModal() == IDOK)
		RunExecutable(dlg.GetExePath(), dlg.GetArguments(), dlg.GetWorkingDir());

	return 0;
}

LRESULT CMainFrame::OnAttachProcess(WORD, WORD, HWND, BOOL&) {
	CProcessSelectDlg dlg;
	if (dlg.DoModal() == IDOK) {
		CString name;
		AttachToProcess(dlg.GetSelectedProcess(name));
	}
	return 0;
}

LRESULT CMainFrame::OnAlwaysOnTop(WORD, WORD id, HWND, BOOL&) {
	bool onTop = GetExStyle() & WS_EX_TOPMOST;
	SetWindowPos(onTop ? HWND_NOTOPMOST : HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
	UISetCheck(id, !onTop);

	return 0;
}

LRESULT CMainFrame::OnToggleDarkMode(WORD, WORD, HWND, BOOL&) {
	WTLHelper::SwitchToMode(WTLHelper::IsDarkMode() ? DarkModeKind::Classic : DarkModeKind::Dark, m_hWnd);
	AppSettings::Get().DarkMode(WTLHelper::IsDarkMode() ? 1 : 0);
	ApplyTheme();

	return 0;
}

LRESULT CMainFrame::OnDebugBreak(WORD, WORD, HWND, BOOL&) {
	DebugManager::DebuggerX().Break();

	return 0;
}

LRESULT CMainFrame::OnDebugGo(WORD, WORD, HWND, BOOL&) {
	DebugManager::DebuggerX().ExecuteCommand(L"g");

	return 0;
}

LRESULT CMainFrame::OnDebugStop(WORD, WORD, HWND, BOOL&) {
	DebugManager::DebuggerX().Stop();

	return 0;
}

LRESULT CMainFrame::OnDebugStepOver(WORD, WORD, HWND, BOOL&) {
	DebugManager::DebuggerX().ExecuteCommand(L"p");
	return 0;
}

LRESULT CMainFrame::OnDebugStepInto(WORD, WORD, HWND, BOOL&) {
	DebugManager::DebuggerX().ExecuteCommand(L"t");
	return 0;
}

LRESULT CMainFrame::OnDebugStepOut(WORD, WORD, HWND, BOOL&) {
	DebugManager::DebuggerX().ExecuteCommand(L"gu");
	return 0;
}

Task<void> CMainFrame::RunExecutable(CString exePath, CString arguments, CString workingDir) {
	auto alive = m_Lifetime.Watch();
	auto& dbg = DebugManager::DebuggerX();

	auto hr = co_await dbg.LaunchAndAttach(exePath, arguments, workingDir);
	if (!*alive)
		co_return;

	if (FAILED(hr)) {
		AtlMessageBox(*this, L"Failed to launch process", IDS_TITLE, MB_ICONERROR);
		co_return;
	}
	co_await dbg.OutputPrompt();
}

Task<void> CMainFrame::AttachToProcess(DWORD pid) {
	auto alive = m_Lifetime.Watch();
	auto& dbg = DebugManager::DebuggerX();

	auto hr = co_await dbg.AttachToProcess(pid);
	if (!*alive)
		co_return;

	if (FAILED(hr)) {
		CString text;
		text.Format(L"Failed to attach to PID %u", pid);
		AtlMessageBox(*this, (PCWSTR)text, IDS_TITLE, MB_ICONERROR);
		co_return;
	}
	co_await dbg.OutputPrompt();
}

Task<void> CMainFrame::AttachLocalKernel() {
	auto alive = m_Lifetime.Watch();
	auto& dbg = DebugManager::DebuggerX();

	auto hr = co_await dbg.AttachLocalKernel();
	if (!*alive)
		co_return;

	if (FAILED(hr)) {
		AtlMessageBox(*this, L"Failed to attach to the local kernel", IDS_TITLE, MB_ICONERROR);
		co_return;
	}
	UIEnable(ID_DEBUG_ATTACHTOKERNEL, false);
	UIEnable(ID_DEBUG_ATTACHTOLOCALKERNEL, false);
	UIEnable(ID_DEBUG_ATTACHTOPROCESS, false);
	UIEnable(ID_DEBUG_RUNEXECUTABLE, false);

	co_await dbg.OutputPrompt();
}

void CMainFrame::InitMenu(HMENU menu) {
	MenuItemData commands[] = {
		{ ID_EDIT_CUT, IDI_CUT },
		{ ID_EDIT_COPY, IDI_COPY },
		{ ID_EDIT_PASTE, IDI_PASTE },
		{ ID_VIEW_REFRESH, IDI_REFRESH },
		{ ID_VIEW_COMMAND, IDI_COMMAND },
		{ ID_VIEW_THREADS, IDI_THREADS },
		{ ID_VIEW_PROCESSES, IDI_PROCESSES },
		{ ID_VIEW_CALLSTACK, IDI_CALLSTACK },
		{ ID_VIEW_BREAKPOINTS, IDI_BREAKPOINTS },
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
		{ ID_WINDOW_CLOSE, IDI_WINDOW_CLOSE },
		{ ID_WINDOW_CLOSE_ALL, IDI_WINDOW_CLOSEALL },
	};
	WTLHelper::InitMenu(menu, commands, _countof(commands));
}

// everything that depends on dark or light: the docking chrome, and the menu's icons (drawn on the menu's background)
void CMainFrame::ApplyTheme() {
	m_Dock.SetTheme(WTLHelper::IsDarkMode() ? DockTheme::Dark() : DockTheme::Light());
	InitMenu(GetMenu());
	DrawMenuBar();
	UISetCheck(ID_OPTIONS_DARKMODE, WTLHelper::IsDarkMode());
}

void CMainFrame::InitDock() {
	m_Dock.SetContentFactory([](DockPane& pane, HWND parent) -> HWND {
		auto info = PaneRegistry::Get().Find(pane.Id());
		return info ? PaneRegistry::Get().CreatePane(*info, parent) : nullptr;
		});

	auto& registry = PaneRegistry::Get();
	registry.LoadStates(AppSettings::Get().PaneStates());

	auto& layout = m_Dock.Layout();
	for (auto& info : registry.Panes()) {
		PaneDesc desc;
		desc.Id = info.Id;
		desc.Title = info.Title;
		desc.Icon = AtlLoadIconImage(info.Icon, LR_DEFAULTCOLOR | LR_SHARED, 16, 16);
		desc.Kind = PaneKind::Tool;
		desc.DefaultSide = info.Side;
		desc.PreferredSize = info.Size;
		layout.AddPane(desc);
	}

	// what the application starts with, and what Window > Reset Window Layout goes back to
	layout.Show(layout.FindPane(L"command"));
	m_Dock.CaptureDefaultLayout();

	// then whatever the last run left, if it did
	auto path = GetLayoutPath();
	if (!path.empty() && ::GetFileAttributes(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
		std::wstring error;
		if (!m_Dock.LoadStateFromFile(path, {}, &error))
			ATLTRACE(L"Layout not restored: %ws\n", error.c_str());
	}
}

void CMainFrame::SaveLayout() {
	auto path = GetLayoutPath();
	if (!path.empty())
		m_Dock.SaveStateToFile(path);
}

void CMainFrame::UpdateUI(bool force) {
	auto& session = DebugManager::Session();
	const auto status = session.Status();
	const auto target = session.Target();
	const auto canRun = session.CanRun();
	const auto attached = status != ExecutionStatus::NoDebuggee;

	UIEnable(ID_DEBUG_GO, canRun, force);
	UIEnable(ID_DEBUG_BREAK, session.IsRunning() || session.IsBusy(), force);
	UIEnable(ID_DEBUG_STOP, attached, force);
	UIEnable(ID_DEBUG_STEPOVER, canRun, force);
	UIEnable(ID_DEBUG_STEPINTO, canRun, force);
	UIEnable(ID_DEBUG_STEPOUT, canRun, force);

	// a pane of a target that is not there is not for showing
	for (auto& info : PaneRegistry::Get().Panes())
		UIEnable(info.Command, PaneRegistry::IsAvailable(info, target), force);
	UIEnable(ID_VIEW_CALLSTACK, attached, force);
	UIEnable(ID_VIEW_BREAKPOINTS, attached, force);
}
