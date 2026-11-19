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
#include "BreakpointDlg.h"
#include "AttachKernelDlg.h"
#include "ExceptionSettingsDlg.h"
#include "EventFilterText.h"
#include "SymbolPathsDlg.h"
#include "SourceView.h"
#include "RecentTargets.h"
#include "SavedBreakpoints.h"
#include <TlHelp32.h>
#include <format>

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
		AtlMessageBox(nullptr, L"Failed to initialize the debugger engine (dbgeng.dll: neither the Windows Kits debugger's nor System32's could be loaded)",
			IDS_TITLE, MB_ICONERROR);
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
		{ ID_DEBUG_RESTART, IDI_REFRESH },
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
	// (which engine it is matters: only the Windows Kits debugger's has symbol servers and the !-commands)
	::SetWindowText(m_hWndStatusBar, (L"Debugger engine: " + DebugManager::EnginePath()).c_str());

	m_hWndClient = m_Dock.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);

	UISetCheck(ID_VIEW_TOOLBAR, 1);
	UISetCheck(ID_VIEW_STATUS_BAR, 1);

	// register object for message filtering and idle updates
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != nullptr);
	pLoop->AddMessageFilter(this);
	pLoop->AddIdleHandler(this);

	DebugManager::Session().AddObserver(this);

	// (the engine starts out preferring DML)
	if (!AppSettings::Get().Dml())
		DebugManager::DebuggerX().SetDmlEnabled(false);
	UISetCheck(ID_OPTIONS_DEBUGGERMARKUPLANGUAGE, AppSettings::Get().Dml() != 0);
	ApplySavedEventFilters();
	ApplySavedSymbolPaths();

	ApplyTheme();
	// (the panes and the saved layout come after WM_CREATE: see InitDock)
	UpdateUI(true);

	return 0;
}

LRESULT CMainFrame::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	DebugManager::Session().RemoveObserver(this);
	m_Sources.Term();
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

LRESULT CMainFrame::OnOpenDumpFile(WORD, WORD, HWND, BOOL&) {
	if (!CanStartSession())
		return 0;

	CFileDialog dlg(TRUE, L"dmp", nullptr,
		OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ENABLESIZING,
		L"Dump Files (*.dmp;*.mdmp;*.hdmp)\0*.dmp;*.mdmp;*.hdmp\0All Files\0*.*\0", *this);
	// the system's file picker does not take the dark mode hook
	WTLHelper::SuspendHook();
	auto ok = dlg.DoModal() == IDOK;
	WTLHelper::ResumeHook();
	if (ok)
		OpenDumpFile(dlg.m_szFileName);

	return 0;
}

LRESULT CMainFrame::OnAttachKernel(WORD, WORD, HWND, BOOL&) {
	if (!CanStartSession())
		return 0;

	CAttachKernelDlg dlg(AppSettings::Get().KernelConnection().c_str());
	if (dlg.DoModal(m_hWnd) == IDOK) {
		AppSettings::Get().KernelConnection((PCWSTR)dlg.GetConnection());
		AttachKernel(dlg.GetConnection());
	}
	return 0;
}

LRESULT CMainFrame::OnDebugDetach(WORD, WORD, HWND, BOOL&) {
	if (DebugManager::Session().HasTarget())
		DebugManager::DebuggerX().Detach();

	return 0;
}

LRESULT CMainFrame::OnToggleDml(WORD, WORD id, HWND, BOOL&) {
	auto enable = !AppSettings::Get().Dml();
	AppSettings::Get().Dml(enable ? 1 : 0);
	DebugManager::DebuggerX().SetDmlEnabled(enable);
	UISetCheck(id, enable);

	return 0;
}

LRESULT CMainFrame::OnAttachLocalKernel(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (!CanStartSession())
		return 0;

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
		auto& layout = m_Dock.Layout();
		if (auto pane = layout.FindPane(info->Id)) {
			// a pane that has never been shown goes in with the ones it belongs with, if they are there
			if (info->TabWith && pane->State() == PaneState::Hidden && !pane->Anchor().Valid) {
				auto with = layout.FindPane(info->TabWith);
				if (with && with->State() != PaneState::Hidden && with->Group())
					layout.DockTo(pane, with->Group(), DockPosition::Tab);
			}
			m_Dock.ShowPane(pane);
		}
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
	if (!CanStartSession())
		return 0;

	CRunExeDlg dlg;
	if (dlg.DoModal() == IDOK)
		RunExecutable(dlg.GetExePath(), dlg.GetArguments(), dlg.GetWorkingDir());

	return 0;
}

LRESULT CMainFrame::OnAttachProcess(WORD, WORD, HWND, BOOL&) {
	if (!CanStartSession())
		return 0;

	CProcessSelectDlg dlg;
	if (dlg.DoModal() == IDOK) {
		CString name;
		auto pid = dlg.GetSelectedProcess(name);
		AttachToProcess(pid, name);
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

// (the commands below can also come from the keyboard, whatever the menu last showed: they check for themselves)
LRESULT CMainFrame::OnDebugBreak(WORD, WORD, HWND, BOOL&) {
	auto& session = DebugManager::Session();
	if (session.IsRunning() || session.IsBusy())
		DebugManager::DebuggerX().Break();

	return 0;
}

LRESULT CMainFrame::OnDebugGo(WORD, WORD, HWND, BOOL&) {
	if (DebugManager::Session().CanRun())
		DebugManager::DebuggerX().ExecuteCommand(L"g");

	return 0;
}

LRESULT CMainFrame::OnDebugStop(WORD, WORD, HWND, BOOL&) {
	if (DebugManager::Session().HasTarget())
		DebugManager::DebuggerX().Stop();

	return 0;
}

LRESULT CMainFrame::OnDebugRestart(WORD, WORD, HWND, BOOL&) {
	if (m_Launch && !m_Restarting && DebugManager::Session().HasTarget())
		Restart();
	else
		::MessageBeep(MB_ICONWARNING);

	return 0;
}

Task<void> CMainFrame::Restart() {
	auto alive = m_Lifetime.Watch();
	auto& session = DebugManager::Session();
	auto& dbg = DebugManager::DebuggerX();
	auto launch = *m_Launch;
	m_Restarting = true;
	UpdateUI();

	// The breakpoints go with the session: they are saved first, to be set again in the new one (as when the program is
	// next run). A running target keeps the engine's thread in WaitForEvent: it is interrupted, or the request would
	// wait for it to stop.
	if (session.IsRunning())
		dbg.Break();
	std::vector<BreakpointInfo> breakpoints;
	co_await dbg.EnumBreakpoints(breakpoints);
	if (!*alive)
		co_return;
	auto key = SavedBreakpoints::KeyOf((PCWSTR)launch.ExePath);
	SavedBreakpoints::Save(key, breakpoints);
	m_BreakpointsKey.clear();
	auto hr = co_await dbg.Stop();
	if (!*alive)
		co_return;
	if (SUCCEEDED(hr))
		hr = co_await dbg.LaunchAndAttach(launch.ExePath, launch.Arguments, launch.WorkingDir);
	if (!*alive)
		co_return;
	m_Restarting = false;
	if (FAILED(hr)) {
		m_Launch.reset();
		UpdateUI();
		CString text;
		text.Format(L"Failed to restart %s (0x%08X)", (PCWSTR)launch.ExePath, static_cast<unsigned>(hr));
		AtlMessageBox(*this, (PCWSTR)text, IDS_TITLE, MB_ICONERROR);
		co_return;
	}
	m_Launch = launch;
	UpdateUI();

	auto failed = co_await RestoreBreakpoints(key);
	if (!*alive)
		co_return;
	co_await dbg.OutputPrompt();
	if (*alive)
		ReportRestoreFailures(failed);
}

Task<int> CMainFrame::RestoreBreakpoints(std::wstring key) {
	auto alive = m_Lifetime.Watch();
	m_BreakpointsKey.clear();
	int failed = 0;
	for (auto& bp : SavedBreakpoints::Load(key)) {
		if (FAILED(co_await DebugManager::DebuggerX().AddBreakpoint(bp)))
			failed++;
		if (!*alive)
			co_return failed;
	}
	// (from now on, what changes is saved: not before, or each one set would save the ones set so far)
	if (DebugManager::Session().HasTarget())
		m_BreakpointsKey = key;
	co_return failed;
}

void CMainFrame::ReportRestoreFailures(int failed) {
	if (failed == 0)
		return;
	CString text;
	text.Format(L"%d breakpoint(s) of the last session could not be set again", failed);
	AtlMessageBox(*this, (PCWSTR)text, IDS_TITLE, MB_ICONWARNING);
}

void CMainFrame::OnBreakpointsChanged() {
	// Only while the target is stopped: when it ends, the engine removes them -- those changes are not the user's
	if (!m_BreakpointsKey.empty() && DebugManager::Session().IsBroken())
		SaveBreakpoints();
}

void CMainFrame::OnTargetEnded() {
	m_BreakpointsKey.clear();
}

Task<void> CMainFrame::SaveBreakpoints() {
	auto alive = m_Lifetime.Watch();
	auto key = m_BreakpointsKey;
	std::vector<BreakpointInfo> breakpoints;
	auto hr = co_await DebugManager::DebuggerX().EnumBreakpoints(breakpoints);
	// (the session can have ended meanwhile: the engine's notice of it comes before this, and then there are none)
	auto& session = DebugManager::Session();
	if (!*alive || FAILED(hr) || key != m_BreakpointsKey || !session.HasTarget() || !session.IsBroken())
		co_return;
	SavedBreakpoints::Save(key, breakpoints);
}

LRESULT CMainFrame::OnDebugStepOver(WORD, WORD, HWND, BOOL&) {
	Step(StepKind::Over);
	return 0;
}

LRESULT CMainFrame::OnDebugStepInto(WORD, WORD, HWND, BOOL&) {
	Step(StepKind::Into);
	return 0;
}

LRESULT CMainFrame::OnDebugStepOut(WORD, WORD, HWND, BOOL&) {
	Step(StepKind::Out);
	return 0;
}

std::wstring CMainFrame::ActivePaneId() const {
	auto pane = m_Dock.ActivePane();
	return pane ? pane->Id() : std::wstring();
}

void CMainFrame::Step(StepKind kind) {
	if (DebugManager::Session().CanRun())
		DebugManager::DebuggerX().Step(kind, ActivePaneId() != L"disassembly");
}

HWND CMainFrame::ActiveTextControl() const {
	auto pane = m_Dock.ActivePane();
	if (!pane || !pane->hWnd)
		return nullptr;
	HWND found = nullptr;
	::EnumChildWindows(pane->hWnd, [](HWND hWnd, LPARAM param) -> BOOL {
		WCHAR cls[64];
		if (::IsWindowVisible(hWnd) && ::GetClassName(hWnd, cls, _countof(cls)) && ::wcsstr(cls, L"Scintilla")) {
			*reinterpret_cast<HWND*>(param) = hWnd;
			return FALSE;
		}
		return TRUE;
		}, reinterpret_cast<LPARAM>(&found));
	return found;
}

void CMainFrame::FindNext(bool up) {
	auto text = ActiveTextControl();
	if (!text || m_Find.Text.empty()) {
		::MessageBeep(MB_ICONWARNING);
		return;
	}
	bool wrapped;
	std::wstring status;
	if (!FindInScintilla(text, m_Find, up, &wrapped)) {
		::MessageBeep(MB_ICONWARNING);
		status = L"Not found: " + m_Find.Text;
	}
	else if (wrapped)
		status = up ? L"Passed the start: continued from the end" : L"Passed the end: continued from the start";
	::SetWindowText(m_hWndStatusBar, status.empty() ? L"Ready" : status.c_str());
}

LRESULT CMainFrame::OnEditFind(WORD, WORD, HWND, BOOL&) {
	auto text = ActiveTextControl();
	if (!text) {
		::MessageBeep(MB_ICONWARNING);
		return 0;
	}
	// (what is selected, if it is a word or so, is what is looked for)
	auto options = m_Find;
	if (auto word = SelectedWord(text); !word.empty())
		options.Text = word;
	CFindDlg dlg(options);
	if (dlg.DoModal(m_hWnd) == IDOK) {
		m_Find = dlg.GetOptions();
		FindNext(m_Find.Up);
	}
	return 0;
}

LRESULT CMainFrame::OnEditFindAgain(WORD, WORD id, HWND, BOOL&) {
	FindNext(id == ID_EDIT_FINDPREV);
	return 0;
}

LRESULT CMainFrame::OnReloadSymbols(WORD, WORD, HWND, BOOL&) {
	auto& session = DebugManager::Session();
	if (session.HasTarget() && !session.IsRunning())
		DebugManager::DebuggerX().ReloadSymbols();
	return 0;
}

LRESULT CMainFrame::OnOpenSourceFile(WORD, WORD, HWND, BOOL&) {
	CFileDialog dlg(TRUE, nullptr, nullptr, OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ENABLESIZING,
		L"C/C++ Files (*.c;*.cpp;*.cxx;*.cc;*.h;*.hpp;*.inl)\0*.c;*.cpp;*.cxx;*.cc;*.h;*.hpp;*.inl\0All Files\0*.*\0", *this);
	// the system's file picker does not take the dark mode hook
	WTLHelper::SuspendHook();
	auto ok = dlg.DoModal() == IDOK;
	WTLHelper::ResumeHook();
	if (ok && !m_Sources.Open(dlg.m_szFileName, 0, true))
		AtlMessageBox(*this, L"Failed to open the file", IDS_TITLE, MB_ICONERROR);
	return 0;
}

LRESULT CMainFrame::OnSymbolPaths(WORD, WORD, HWND, BOOL&) {
	// (the engine's thread is in WaitForEvent while the target runs: the paths could not be read until it stops)
	if (DebugManager::Session().IsRunning()) {
		::MessageBeep(MB_ICONWARNING);
		return 0;
	}
	EditSymbolPaths();
	return 0;
}

Task<void> CMainFrame::EditSymbolPaths() {
	auto alive = m_Lifetime.Watch();
	auto& dbg = DebugManager::DebuggerX();

	std::wstring symbolPath, sourcePath;
	auto hr = co_await dbg.GetSymbolPaths(symbolPath, sourcePath);
	if (!*alive)
		co_return;
	if (FAILED(hr)) {
		AtlMessageBox(*this, L"Failed to read the symbol and source paths from the engine", IDS_TITLE, MB_ICONERROR);
		co_return;
	}

	CSymbolPathsDlg dlg(symbolPath, sourcePath);
	if (dlg.DoModal(m_hWnd) != IDOK)
		co_return;

	auto newSymbolPath = dlg.GetSymbolPath(), newSourcePath = dlg.GetSourcePath();
	if (newSymbolPath != symbolPath)
		hr = co_await dbg.SetSymbolPath(newSymbolPath.c_str());
	if (SUCCEEDED(hr) && newSourcePath != sourcePath)
		hr = co_await dbg.SetSourcePath(newSourcePath.c_str());
	if (!*alive)
		co_return;
	if (FAILED(hr)) {
		CString text;
		text.Format(L"Failed to change the paths (0x%08X)", static_cast<unsigned>(hr));
		AtlMessageBox(*this, (PCWSTR)text, IDS_TITLE, MB_ICONERROR);
		co_return;
	}
	AppSettings::Get().SymbolPath(newSymbolPath);
	AppSettings::Get().SourcePath(newSourcePath);

	// symbols that were looked for (and not found) with the old path are looked for again; with them, what the panes
	// show (ReloadSymbols says the context has changed); a new source path may find files that were not found
	auto& session = DebugManager::Session();
	if (newSymbolPath != symbolPath && session.HasTarget() && !session.IsRunning())
		co_await dbg.ReloadSymbols();
	else if (newSourcePath != sourcePath && session.HasTarget() && session.IsBroken())
		m_Sources.OnContextChanged();
}

void CMainFrame::ApplySavedSymbolPaths() {
	auto& settings = AppSettings::Get();
	if (!settings.SymbolPath().empty())
		DebugManager::DebuggerX().SetSymbolPath(settings.SymbolPath().c_str());
	if (!settings.SourcePath().empty())
		DebugManager::DebuggerX().SetSourcePath(settings.SourcePath().c_str());
}

bool CMainFrame::CanStartSession() const {
	auto& session = DebugManager::Session();
	return !session.HasTarget() && !session.IsBusy() && !m_Restarting;
}

LRESULT CMainFrame::OnDebugExceptions(WORD, WORD, HWND, BOOL&) {
	// (the engine's thread is in WaitForEvent while the target runs: the filters could not be read until it stops)
	if (DebugManager::Session().IsRunning()) {
		::MessageBeep(MB_ICONWARNING);
		return 0;
	}
	EditExceptionSettings();
	return 0;
}

Task<void> CMainFrame::EditExceptionSettings() {
	auto alive = m_Lifetime.Watch();
	auto& dbg = DebugManager::DebuggerX();

	std::vector<EventFilter> filters, defaults;
	auto hr = co_await dbg.GetEventFilters(filters);
	if (!*alive)
		co_return;
	if (SUCCEEDED(hr))
		hr = co_await dbg.GetEventFilters(defaults, true);
	if (!*alive)
		co_return;
	if (FAILED(hr)) {
		AtlMessageBox(*this, L"Failed to read the exception settings from the engine", IDS_TITLE, MB_ICONERROR);
		co_return;
	}

	CExceptionSettingsDlg dlg(std::move(filters), defaults);
	if (dlg.DoModal(m_hWnd) != IDOK)
		co_return;

	auto chosen = dlg.GetFilters();
	hr = co_await dbg.SetEventFilters(chosen);
	if (!*alive)
		co_return;
	if (FAILED(hr)) {
		CString text;
		text.Format(L"Failed to change the exception settings (0x%08X)", static_cast<unsigned>(hr));
		AtlMessageBox(*this, (PCWSTR)text, IDS_TITLE, MB_ICONERROR);
		co_return;
	}
	AppSettings::Get().EventFilters(EventFilterText::Save(chosen, defaults));
}

Task<void> CMainFrame::ApplySavedEventFilters() {
	auto text = AppSettings::Get().EventFilters();
	if (text.empty())
		co_return;

	auto& dbg = DebugManager::DebuggerX();
	std::vector<EventFilter> defaults;
	co_await dbg.GetEventFilters(defaults, true);
	// (nothing of the window's is used after this: no need to watch its lifetime)
	co_await dbg.SetEventFilters(EventFilterText::Load(text, defaults));
}

LRESULT CMainFrame::OnDebugAddBreakpoint(WORD, WORD, HWND, BOOL&) {
	if (!DebugManager::Session().HasTarget())
		return 0;

	// F9 in a source file is on the line with the caret, as in other debuggers
	// (and in the Disassembly window, on the instruction)
	if (SourceDocuments::IsSourceId(ActivePaneId()) || ActivePaneId() == L"disassembly") {
		if (auto pane = m_Dock.ActivePane(); pane->hWnd)
			::SendMessage(pane->hWnd, WM_COMMAND, ID_DISASM_TOGGLEBREAKPOINT, 0);
		return 0;
	}

	CBreakpointDlg dlg;
	if (dlg.DoModal(m_hWnd) == IDOK)
		AddBreakpoint(dlg.GetSettings());

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
	m_Launch = Launch{ exePath, arguments, workingDir };
	RecentTargets::Add({ RecentTarget::Kind::Run, (PCWSTR)exePath, (PCWSTR)arguments, (PCWSTR)workingDir });
	UpdateUI();
	auto failed = co_await RestoreBreakpoints(SavedBreakpoints::KeyOf((PCWSTR)exePath));
	if (!*alive)
		co_return;
	co_await dbg.OutputPrompt();
	if (*alive)
		ReportRestoreFailures(failed);
}

Task<void> CMainFrame::AttachToProcess(DWORD pid, CString name) {
	auto alive = m_Lifetime.Watch();
	auto& dbg = DebugManager::DebuggerX();

	m_Launch.reset();		// (no Restart for it)
	// (whose breakpoints: asked now, while the process is there)
	auto key = SavedBreakpoints::KeyOf(SavedBreakpoints::ImagePathOf(pid));
	auto hr = co_await dbg.AttachToProcess(pid);
	if (!*alive)
		co_return;

	if (FAILED(hr)) {
		CString text;
		text.Format(L"Failed to attach to PID %u", pid);
		AtlMessageBox(*this, (PCWSTR)text, IDS_TITLE, MB_ICONERROR);
		co_return;
	}
	if (!name.IsEmpty())
		RecentTargets::Add({ RecentTarget::Kind::Attach, (PCWSTR)name });
	auto failed = co_await RestoreBreakpoints(key);
	if (!*alive)
		co_return;
	co_await dbg.OutputPrompt();
	if (*alive)
		ReportRestoreFailures(failed);
}

Task<void> CMainFrame::AttachLocalKernel() {
	auto alive = m_Lifetime.Watch();
	auto& dbg = DebugManager::DebuggerX();

	m_Launch.reset();		// (no Restart for it)
	auto hr = co_await dbg.AttachLocalKernel();
	if (!*alive)
		co_return;

	if (FAILED(hr)) {
		AtlMessageBox(*this, L"Failed to attach to the local kernel", IDS_TITLE, MB_ICONERROR);
		co_return;
	}
	RecentTargets::Add({ RecentTarget::Kind::LocalKernel });
	co_await dbg.OutputPrompt();
}

Task<void> CMainFrame::AttachKernel(CString connection) {
	auto alive = m_Lifetime.Watch();
	auto& dbg = DebugManager::DebuggerX();

	m_Launch.reset();		// (no Restart for it)
	auto hr = co_await dbg.AttachKernel(connection);
	if (!*alive)
		co_return;

	if (FAILED(hr)) {
		CString text;
		text.Format(L"Failed to attach to the kernel with \"%s\" (0x%08X)", (PCWSTR)connection, static_cast<unsigned>(hr));
		AtlMessageBox(*this, (PCWSTR)text, IDS_TITLE, MB_ICONERROR);
		co_return;
	}
	RecentTargets::Add({ RecentTarget::Kind::Kernel, (PCWSTR)connection });
	co_await dbg.OutputPrompt();
}

Task<void> CMainFrame::OpenDumpFile(CString path) {
	auto alive = m_Lifetime.Watch();
	auto& dbg = DebugManager::DebuggerX();

	m_Launch.reset();		// (no Restart for it)
	auto hr = co_await dbg.OpenDumpFile(path);
	if (!*alive)
		co_return;

	if (FAILED(hr)) {
		CString text;
		text.Format(L"Failed to open %s (0x%08X)", (PCWSTR)path, static_cast<unsigned>(hr));
		AtlMessageBox(*this, (PCWSTR)text, IDS_TITLE, MB_ICONERROR);
		co_return;
	}
	RecentTargets::Add({ RecentTarget::Kind::Dump, (PCWSTR)path });
	co_await dbg.OutputPrompt();
}

LRESULT CMainFrame::OnInitMenuPopup(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	// (the rest of the menus are CAutoUpdateUI's)
	bHandled = FALSE;
	CMenuHandle menu(reinterpret_cast<HMENU>(wParam));
	auto first = menu.GetMenuItemID(0);
	if (first != ID_FILE_RECENT1 && first != ID_FILE_CLEARRECENT)
		return 0;

	// Recent Targets: made again each time it opens
	while (menu.GetMenuItemCount() > 0)
		menu.DeleteMenu(0, MF_BYPOSITION);
	auto targets = RecentTargets::Load();
	const UINT enabled = CanStartSession() ? MF_ENABLED : MF_GRAYED;
	for (size_t i = 0; i < targets.size(); i++) {
		auto text = (i < 9 ? std::format(L"&{} ", i + 1) : std::wstring(L"1&0 ")) + targets[i].MenuText();
		menu.AppendMenu(MF_STRING | enabled, ID_FILE_RECENT1 + static_cast<UINT>(i), text.c_str());
	}
	if (targets.empty())
		menu.AppendMenu(MF_STRING | MF_GRAYED, ID_FILE_RECENT1, L"(none)");
	else {
		menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(MF_STRING, ID_FILE_CLEARRECENT, L"&Clear List");
	}
	return 0;
}

LRESULT CMainFrame::OnClearRecent(WORD, WORD, HWND, BOOL&) {
	RecentTargets::Clear();
	UpdateUI();
	return 0;
}

LRESULT CMainFrame::OnRecentTarget(WORD, WORD id, HWND, BOOL&) {
	auto targets = RecentTargets::Load();
	const size_t index = id - ID_FILE_RECENT1;
	if (index >= targets.size() || !CanStartSession()) {
		::MessageBeep(MB_ICONWARNING);
		return 0;
	}

	auto& target = targets[index];
	switch (target.Type) {
		case RecentTarget::Kind::Run:
			RunExecutable(target.Path.c_str(), target.Arguments.c_str(), target.WorkingDir.c_str());
			break;

		case RecentTarget::Kind::Dump:
			OpenDumpFile(target.Path.c_str());
			break;

		case RecentTarget::Kind::Kernel:
			AttachKernel(target.Path.c_str());
			break;

		case RecentTarget::Kind::LocalKernel:
			// (elevation is asked for there)
			PostMessage(WM_COMMAND, ID_DEBUG_ATTACHTOLOCALKERNEL);
			break;

		case RecentTarget::Kind::Attach: {
			// the running process with that name, if there is just one
			std::vector<DWORD> pids;
			wil::unique_handle snapshot(::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
			PROCESSENTRY32W pe{ sizeof(pe) };
			if (snapshot && ::Process32FirstW(snapshot.get(), &pe)) {
				do {
					if (_wcsicmp(pe.szExeFile, target.Path.c_str()) == 0 && pe.th32ProcessID != ::GetCurrentProcessId())
						pids.push_back(pe.th32ProcessID);
				} while (::Process32NextW(snapshot.get(), &pe));
			}
			if (pids.size() == 1)
				AttachToProcess(pids[0], target.Path.c_str());
			else {
				auto text = pids.empty() ? std::format(L"No process named {} is running.", target.Path)
					: std::format(L"{} processes named {} are running: choose one with Attach to Process.", pids.size(), target.Path);
				AtlMessageBox(*this, text.c_str(), IDS_TITLE, MB_ICONINFORMATION);
			}
			break;
		}
	}
	return 0;
}

LRESULT CMainFrame::OnSaveDump(WORD, WORD, HWND, BOOL&) {
	auto& session = DebugManager::Session();
	if ((session.Target() & TargetType::Live) != TargetType::Live || !session.IsBroken() || session.IsBusy()) {
		::MessageBeep(MB_ICONWARNING);
		return 0;
	}
	SaveDump();
	return 0;
}

Task<void> CMainFrame::SaveDump() {
	auto alive = m_Lifetime.Watch();
	auto& dbg = DebugManager::DebuggerX();
	const bool kernel = (DebugManager::Session().Target() & TargetType::Kernel) == TargetType::Kernel;

	// named after the process, and when
	std::wstring name = L"kernel";
	if (!kernel) {
		std::vector<ProcessInfo> processes;
		std::vector<ThreadInfo> threads;
		co_await dbg.EnumProcesses(processes, threads);
		if (!*alive)
			co_return;
		if (!processes.empty() && !processes[0].ImagePath.empty()) {
			name = processes[0].ImagePath.substr(processes[0].ImagePath.find_last_of(L"\\/") + 1);
			if (auto dot = name.find_last_of(L'.'); dot != std::wstring::npos)
				name.erase(dot);
		}
	}
	auto fileName = std::format(L"{}_{}.dmp", name, (PCWSTR)CTime::GetCurrentTime().Format(L"%Y%m%d_%H%M%S"));

	// (which kind: the filter chosen)
	CFileDialog dlg(FALSE, L"dmp", fileName.c_str(), OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_ENABLESIZING,
		kernel ? L"Full Kernel Dump (*.dmp)\0*.dmp\0Small Kernel Dump (*.dmp)\0*.dmp\0"
			: L"Full Dump: all memory (*.dmp)\0*.dmp\0Minidump (*.dmp)\0*.dmp\0", *this);
	WTLHelper::SuspendHook();
	auto ok = dlg.DoModal() == IDOK;
	WTLHelper::ResumeHook();
	if (!ok)
		co_return;
	const bool full = dlg.m_ofn.nFilterIndex != 2;
	std::wstring path(dlg.m_szFileName);

	::SetWindowText(m_hWndStatusBar, std::format(L"Writing {}...", path).c_str());
	auto hr = co_await dbg.WriteDump(path.c_str(), full);
	if (!*alive)
		co_return;
	if (FAILED(hr)) {
		::SetWindowText(m_hWndStatusBar, L"Ready");
		AtlMessageBox(*this, std::format(L"Failed to write {} (0x{:08X})", path, static_cast<unsigned>(hr)).c_str(), IDS_TITLE, MB_ICONERROR);
		co_return;
	}
	::SetWindowText(m_hWndStatusBar, std::format(L"Dump written: {}", path).c_str());
}

LRESULT CMainFrame::OnAddWatch(UINT, WPARAM, LPARAM lParam, BOOL&) {
	// the Watch pane shown (made, the first time), then told
	BOOL handled;
	OnViewPane(0, ID_VIEW_WATCH, nullptr, handled);
	if (auto pane = m_Dock.Layout().FindPane(L"watch"); pane && pane->hWnd)
		::SendMessage(pane->hWnd, WM_ADDWATCH, 0, lParam);
	return 0;
}

Task<void> CMainFrame::AddBreakpoint(BreakpointSettings settings) {
	auto alive = m_Lifetime.Watch();

	auto hr = co_await DebugManager::DebuggerX().AddBreakpoint(settings);
	if (!*alive)
		co_return;

	if (FAILED(hr))
		AtlMessageBox(*this, (PCWSTR)BreakpointErrorText(settings, hr), IDS_TITLE, MB_ICONERROR);
	// (the list of breakpoints hears of it from the engine)
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
		{ ID_VIEW_KERNELTHREADS, IDI_THREADS },
		{ ID_VIEW_OUTPUT, IDI_EVENT },
		{ ID_VIEW_MODULES, IDI_DLL },
		{ ID_VIEW_REGISTERS, IDI_REGISTRY },
		{ ID_DEBUG_ADDBREAKPOINT, IDI_BREAKPOINT },
		{ ID_VIEW_CALLSTACK, IDI_CALLSTACK },
		{ ID_VIEW_BREAKPOINTS, IDI_BREAKPOINTS },
		{ ID_VIEW_MEMORY, IDI_MEMORY },
		{ ID_VIEW_DISASSEMBLY, IDI_STRUCT },
		{ ID_VIEW_LOCALS, IDI_TOOL },
		{ ID_VIEW_WATCH, IDI_FIND },
		{ ID_FILE_OPENSOURCE, IDI_OPEN },
		{ ID_DEBUG_ATTACHTOPROCESS, IDI_PROCESS_ADD },
		{ ID_DEBUG_RUNEXECUTABLE, IDI_PROCESS_NEW },
		{ ID_DEBUG_ATTACHTOLOCALKERNEL, IDI_LOCAL_KERNEL },
		{ ID_DEBUG_ATTACHTOKERNEL, IDI_ATTACH_KERNEL },
		{ ID_DEBUG_GO, IDI_GO },
		{ ID_DEBUG_BREAK, IDI_PAUSE },
		{ ID_DEBUG_STOP, IDI_STOP },
		{ ID_DEBUG_RESTART, IDI_REFRESH },
		{ ID_DEBUG_SETNEXTSTATEMENT, IDI_ICON9 },
		{ ID_EDIT_FIND, IDI_FIND },
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
	m_Sources.Init(m_Dock, m_hWndStatusBar);
	m_Dock.SetContentFactory([this](DockPane& pane, HWND parent) -> HWND {
		if (SourceDocuments::IsSourceId(pane.Id()))
			return m_Sources.CreateView(pane, parent);
		auto info = PaneRegistry::Get().Find(pane.Id());
		return info ? PaneRegistry::Get().CreatePane(*info, parent) : nullptr;
		});
	// the source files that were open last time (the layout names them; their panes are made as it is loaded)
	m_Dock.SetPaneFactory([this](DockLayout& layout, const std::wstring& id) {
		return m_Sources.MakePane(layout, id);
		});
	m_Dock.OnPaneClosed = [this](DockPane* pane) {
		m_Sources.OnPaneClosed(pane);
		};

	auto& registry = PaneRegistry::Get();
	registry.LoadStates(AppSettings::Get().PaneStates());

	auto& layout = m_Dock.Layout();
	for (auto& info : registry.Panes()) {
		PaneDesc desc;
		desc.Id = info.Id;
		desc.Title = info.Title;
		desc.Icon = AtlLoadIconImage(info.Icon, LR_DEFAULTCOLOR | LR_SHARED, 16, 16);
		desc.Kind = info.Kind;
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
	const auto canStart = CanStartSession();
	const auto isDump = (target & TargetType::Dump) == TargetType::Dump;

	UIEnable(ID_FILE_OPEN, canStart, force);
	UIEnable(ID_DEBUG_ATTACHTOPROCESS, canStart, force);
	UIEnable(ID_DEBUG_RUNEXECUTABLE, canStart, force);
	UIEnable(ID_DEBUG_ATTACHTOLOCALKERNEL, canStart, force);
	UIEnable(ID_DEBUG_ATTACHTOKERNEL, canStart, force);

	UIEnable(ID_DEBUG_GO, canRun, force);
	UIEnable(ID_DEBUG_BREAK, session.IsRunning() || session.IsBusy(), force);
	UIEnable(ID_DEBUG_STOP, attached, force);
	UIEnable(ID_DEBUG_DETACH, session.HasTarget() && !isDump, force);
	UIEnable(ID_DEBUG_SAVEDUMP, (target & TargetType::Live) == TargetType::Live && session.IsBroken() && !session.IsBusy(), force);
	// (Recent Targets is made when it opens, OnInitMenuPopup; its first item is in the menu resource, so it is CAutoUpdateUI's
	// as well, which would otherwise enable it)
	UIEnable(ID_FILE_RECENT1, canStart && !RecentTargets::Load().empty(), force);
	UIEnable(ID_VIEW_REFRESH, session.HasTarget(), force);
	UIEnable(ID_DEBUG_STEPOVER, canRun, force);
	UIEnable(ID_DEBUG_STEPINTO, canRun, force);
	UIEnable(ID_DEBUG_STEPOUT, canRun, force);
	UIEnable(ID_DEBUG_RUNTOCURSOR, canRun, force);
	UIEnable(ID_DEBUG_SETNEXTSTATEMENT, canRun, force);
	UIEnable(ID_DEBUG_RESTART, m_Launch.has_value() && !m_Restarting && session.HasTarget(), force);
	UIEnable(ID_DEBUG_RELOADSYMBOLS, session.HasTarget() && !session.IsRunning(), force);
	UIEnable(ID_OPTIONS_SYMBOLPATHS, !session.IsRunning(), force);

	// a pane of a target that is not there is not for showing
	for (auto& info : PaneRegistry::Get().Panes())
		UIEnable(info.Command, PaneRegistry::IsAvailable(info, target), force);
	UIEnable(ID_DEBUG_ADDBREAKPOINT, attached, force);
	UIEnable(ID_DEBUG_EXCEPTIONS, !session.IsRunning(), force);
}
