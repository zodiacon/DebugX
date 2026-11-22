// MainFrm.cpp : implmentation of the CMainFrame class
//
/////////////////////////////////////////////////////////////////////////////

#include "pch.h"
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
#include "InputDlg.h"
#include "AppFonts.h"
#include <TlHelp32.h>
#include <format>

using namespace DebugEngine;
using namespace WTLDock;

namespace {
	// a file in %LOCALAPPDATA%\DebugX: layout.json is the arrangement of the panes and the frame the last run left,
	// mode-layouts.json the last layout of each mode (user / kernel), layouts.json the ones saved by name
	std::wstring GetDataPath(PCWSTR name) {
		wil::unique_cotaskmem_string folder;
		if (FAILED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &folder)))
			return {};

		std::wstring dir(folder.get());
		dir += L"\\DebugX";
		::CreateDirectory(dir.c_str(), nullptr);
		return dir + L"\\" + name;
	}

	bool FileExists(const std::wstring& path) {
		return !path.empty() && ::GetFileAttributes(path.c_str()) != INVALID_FILE_ATTRIBUTES;
	}

	// layouts saved before there were default layouts are not restored
	LoadOptions CurrentLayoutOptions() {
		LoadOptions options;
		options.MinAppVersion = LayoutVersion;
		return options;
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
	UpdateTitle();
}

void CMainFrame::LayoutStatusParts() {
	if (!m_hWndStatusBar)
		return;
	CStatusBarCtrl sb(m_hWndStatusBar);
	// (the frame's width: on WM_SIZE the status bar has not been made as wide yet)
	CRect rc;
	GetClientRect(&rc);
	// (from the right: the engine, the target, the status; the messages have the rest)
	const int dpi = ::GetDpiForWindow(m_hWnd);
	const int widths[] = { ::MulDiv(90, dpi, 96), ::MulDiv(110, dpi, 96), ::MulDiv(150, dpi, 96) };
	int parts[4];
	parts[3] = -1;
	parts[2] = rc.right - widths[2] - ::GetSystemMetricsForDpi(SM_CXVSCROLL, dpi);	// (the grip)
	parts[1] = parts[2] - widths[1];
	parts[0] = std::max(parts[1] - widths[0], 0);
	sb.SetParts(_countof(parts), parts);
}

void CMainFrame::UpdateStatusParts() {
	if (!m_hWndStatusBar)
		return;
	CStatusBarCtrl sb(m_hWndStatusBar);
	auto& session = DebugManager::Session();
	const auto target = session.Target();

	PCWSTR status = L"";
	if (m_Connecting)
		status = L"Connecting";
	else if (!session.HasTarget())
		status = L"No target";
	else if (session.IsRunning())
		status = L"Running";
	else if (session.IsBusy())
		status = L"Busy";
	else if (session.IsBroken())
		status = L"Break";

	PCWSTR kind = L"";
	if (target == TargetType::LocalKernel)
		kind = L"Local kernel";
	else if ((target & TargetType::DumpKernel) == TargetType::DumpKernel)
		kind = L"Kernel dump";
	else if ((target & TargetType::DumpUser) == TargetType::DumpUser)
		kind = L"User dump";
	else if ((target & TargetType::LiveKernel) == TargetType::LiveKernel)
		kind = L"Kernel";
	else if ((target & TargetType::LiveUser) == TargetType::LiveUser)
		kind = m_ProcessServer.IsEmpty() ? L"User mode" : L"User mode (remote)";

	// which dbgeng.dll: the Windows Kits debugger's, System32's, or another's
	auto& path = DebugManager::EnginePath();
	PCWSTR engine = L"Engine: other";
	if (path.find(L"\\Windows Kits\\") != std::wstring::npos)
		engine = L"Engine: Windows Kits";
	else if (WCHAR system[MAX_PATH]; ::GetSystemDirectoryW(system, _countof(system)) && _wcsnicmp(path.c_str(), system, wcslen(system)) == 0)
		engine = L"Engine: System32";

	sb.SetText(1, status);
	sb.SetText(2, kind);
	sb.SetText(3, engine);
}

LRESULT CMainFrame::OnSize(UINT, WPARAM, LPARAM, BOOL& handled) {
	handled = FALSE;	// (the frame lays out its bars and client)
	LayoutStatusParts();
	return 0;
}

void CMainFrame::SetTargetName(CString name) {
	m_TargetName = name;
	UpdateTitle();
}

void CMainFrame::UpdateTitle() {
	// "notepad.exe (1234) [Break] - DebugX (Administrator)": which instance is debugging what, on the taskbar too
	CString title;
	auto& session = DebugManager::Session();
	if (!m_TargetName.IsEmpty() && session.HasTarget()) {
		title = m_TargetName;
		if (session.IsRunning())
			title += L" [Running]";
		else if (session.IsBroken())
			title += L" [Break]";
		title += L" - ";
	}
	title += m_BaseTitle;
	CString current;
	GetWindowText(current);
	if (current != title)
		SetWindowText(title);
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

	if (SecurityHelper::IsRunningElevated()) {
		// File > Run As Administrator (the first item) and its separator: nothing to do
		CMenuHandle file = CMenuHandle(GetMenu()).GetSubMenu(0);
		file.DeleteMenu(ID_FILE_RUNASADMINISTRATOR, MF_BYCOMMAND);
		file.DeleteMenu(0, MF_BYPOSITION);
		CString title;
		GetWindowText(title);
		SetWindowText(title + L" (Administrator)");
	}
	// (UpdateTitle puts the target before it)
	GetWindowText(m_BaseTitle);

	// files dropped on the window (OnDropFiles) -- also from Explorer when DebugX runs elevated and Explorer does not,
	// which the system filters out unless let through
	DragAcceptFiles(TRUE);
	for (UINT msg : { UINT(WM_DROPFILES), UINT(WM_COPYDATA), UINT(0x0049) /* WM_COPYGLOBALDATA */ })
		::ChangeWindowMessageFilterEx(m_hWnd, msg, MSGFLT_ALLOW, nullptr);
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
		// (the arrow of the current line)
		{ ID_DEBUG_SHOWNEXT, IDI_ICON9 },
	};
	// the panes of the View menu, in its order and groups (the icons are the panes' own; Memory 2-4 stay in the menu)
	static const UINT paneCommands[] = {
		ID_VIEW_COMMAND, ID_VIEW_OUTPUT, ID_VIEW_ANALYSIS, 0,
		ID_VIEW_THREADS, ID_VIEW_USERPROCESSES, ID_VIEW_HANDLES, 0,
		ID_VIEW_PROCESSES, ID_VIEW_KERNELTHREADS, ID_VIEW_DRIVERS, ID_VIEW_OBJECTS, ID_VIEW_IRPS, 0,
		ID_VIEW_MODULES, ID_VIEW_REGISTERS, ID_VIEW_CALLSTACK, ID_VIEW_BREAKPOINTS, ID_VIEW_MEMORY,
		ID_VIEW_DISASSEMBLY, ID_VIEW_LOCALS, ID_VIEW_WATCH,
	};
	std::vector<ToolBarButtonInfo> paneButtons;
	for (auto id : paneCommands) {
		auto info = id ? PaneRegistry::Get().FindByCommand(id) : nullptr;
		ATLASSERT(id == 0 || info);
		paneButtons.push_back({ id, info ? int(info->Icon) : 0 });
	}

	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	// the frame is per-monitor DPI aware: size the images for the monitor it is on
	const int imageSize = ::MulDiv(24, ::GetDpiForWindow(m_hWnd), 96);
	auto tb = ToolbarHelper::CreateAndInitToolBar(m_hWnd, buttons, _countof(buttons), imageSize);
	UIAddToolBar(tb);
	AddSimpleReBarBand(tb);
	// (a band of its own, which the rebar puts on a row of its own when the window is too narrow for both)
	auto panesTb = ToolbarHelper::CreateAndInitToolBar(m_hWnd, paneButtons.data(), int(paneButtons.size()), imageSize);
	UIAddToolBar(panesTb);
	AddSimpleReBarBand(panesTb);
	CreateSimpleStatusBar();
	// Parts: the messages (SetWindowText is the first part's text), then what the session is (UpdateStatusParts).
	// (which engine it is matters: only the Windows Kits debugger's has symbol servers and the !-commands -- its path is
	// the first message, and stays in its own part, shorter)
	LayoutStatusParts();
	::SetWindowText(m_hWndStatusBar, (L"Debugger engine: " + DebugManager::EnginePath()).c_str());
	UpdateStatusParts();

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
		DebugManager::Debugger().SetDmlEnabled(false);
	UISetCheck(ID_OPTIONS_DEBUGGERMARKUPLANGUAGE, AppSettings::Get().Dml() != 0);
	if (AppSettings::Get().DebugChildProcesses())
		DebugManager::Debugger().SetDebugChildProcesses(true);
	UISetCheck(ID_OPTIONS_DEBUGCHILDPROCESSES, AppSettings::Get().DebugChildProcesses() != 0);
	if (AppSettings::Get().HexDisplay())
		DebugManager::Debugger().SetDisplayRadix(16);
	UISetCheck(ID_OPTIONS_HEXDISPLAY, AppSettings::Get().HexDisplay() != 0);
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

void CMainFrame::SaveState() {
	AppSettings::Get().PaneStates(PaneRegistry::Get().SaveStates());
	SaveLayout();
}

LRESULT CMainFrame::OnClose(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	SaveState();

	// the target is detached when the debugger is destroyed, see OnFinalMessage
	bHandled = FALSE;

	return 0;
}

LRESULT CMainFrame::OnFileExit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	PostMessage(WM_CLOSE);
	return 0;
}

bool CMainFrame::RelaunchElevated() {
	// saved before the new instance starts, which reads them (closing this one saves them again, the same)
	SaveState();
	AppSettings::Get().Save();
	if (!SecurityHelper::RunElevated(nullptr, true))
		return false;

	PostMessage(WM_CLOSE);
	return true;
}

void CMainFrame::CancelKernelConnection() {
	if (AtlMessageBox(*this, L"The debugger engine cannot stop waiting for the kernel to connect: DebugX has to start again to "
		L"give up the connection.\n\nRestart DebugX?", IDS_TITLE, MB_ICONQUESTION | MB_OKCANCEL) != IDOK || !m_Connecting)
		return;		// (or the target connected meanwhile)

	// saved before the new instance starts, which reads them
	SaveState();
	AppSettings::Get().Save();
	WCHAR path[MAX_PATH];
	::GetModuleFileName(nullptr, path, _countof(path));
	STARTUPINFO si{ sizeof(si) };
	PROCESS_INFORMATION pi;
	// (with this one's elevation, if it has it)
	if (!::CreateProcess(path, nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
		AtlMessageBox(*this, L"DebugX could not be started again.", IDS_TITLE, MB_ICONERROR);
		return;
	}
	::CloseHandle(pi.hThread);
	::CloseHandle(pi.hProcess);
	// Not a close: the debug thread is inside the engine's wait, which a shutdown would wait for (and then leave the
	// engine as it is, in the middle of it). The kernel connection's socket goes with the process.
	::TerminateProcess(::GetCurrentProcess(), 0);
}

LRESULT CMainFrame::OnRunAsAdministrator(WORD, WORD, HWND, BOOL&) {
	if (DebugManager::Session().HasTarget()
		&& AtlMessageBox(*this, L"Running as administrator ends the debugging session. Continue?", IDS_TITLE,
			MB_ICONQUESTION | MB_OKCANCEL) != IDOK)
		return 0;

	RelaunchElevated();
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
		DebugManager::Debugger().Detach();

	return 0;
}

LRESULT CMainFrame::OnChooseFont(WORD, WORD id, HWND, BOOL&) {
	// (the windows that use the font take it again from AppFonts::FontsChangedMessage)
	AppFonts::Choose(id == ID_OPTIONS_CODEFONT ? AppFonts::Kind::Code : AppFonts::Kind::Ui, m_hWnd);
	return 0;
}

LRESULT CMainFrame::OnToggleDml(WORD, WORD id, HWND, BOOL&) {
	auto enable = !AppSettings::Get().Dml();
	AppSettings::Get().Dml(enable ? 1 : 0);
	DebugManager::Debugger().SetDmlEnabled(enable);
	UISetCheck(id, enable);

	return 0;
}

LRESULT CMainFrame::OnToggleChildProcesses(WORD, WORD id, HWND, BOOL&) {
	// (from the next launch on; the session there is too)
	auto debug = !AppSettings::Get().DebugChildProcesses();
	AppSettings::Get().DebugChildProcesses(debug ? 1 : 0);
	DebugManager::Debugger().SetDebugChildProcesses(debug);
	UISetCheck(id, debug);
	return 0;
}

LRESULT CMainFrame::OnToggleHexDisplay(WORD, WORD id, HWND, BOOL&) {
	auto hex = !AppSettings::Get().HexDisplay();
	AppSettings::Get().HexDisplay(hex ? 1 : 0);
	DebugManager::Debugger().SetDisplayRadix(hex ? 16 : 10);
	UISetCheck(id, hex);
	// the panes that show values, again (one that is hidden refreshes when it is shown)
	for (auto paneId : { L"locals", L"watch" })
		if (auto pane = m_Dock.Layout().FindPane(paneId); pane && pane->hWnd)
			::SendMessage(pane->hWnd, WM_COMMAND, ID_VIEW_REFRESH, 0);
	return 0;
}

LRESULT CMainFrame::OnAttachLocalKernel(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (!CanStartSession())
		return 0;

	if (!SecurityHelper::IsRunningElevated()) {
		int reply = AtlMessageBox(*this, L"Local kernel debugging requires admin privileges. Run elevated now?",
			IDS_TITLE, MB_ICONEXCLAMATION | MB_YESNO);
		if (reply == IDYES)
			RelaunchElevated();
		return 0;
	}
	AttachLocalKernel();

	return 0;
}

LRESULT CMainFrame::OnViewToolBar(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	static BOOL bVisible = TRUE;	// initially visible
	bVisible = !bVisible;
	CReBarCtrl rebar = m_hWndToolBar;
	// (the debug commands' toolbar and the panes')
	for (UINT i = 0; i < rebar.GetBandCount(); i++)
		rebar.ShowBand(i, bVisible);
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

LRESULT CMainFrame::OnToolTipText(int, LPNMHDR pnmh, BOOL& bHandled) {
	auto info = reinterpret_cast<NMTTDISPINFOW*>(pnmh);
	WCHAR item[128]{};
	if ((info->uFlags & TTF_IDISHWND) == 0 && pnmh->idFrom)
		::GetMenuString(GetMenu(), UINT(pnmh->idFrom), item, _countof(item), MF_BYCOMMAND);
	CString text(item);
	if (text.IsEmpty()) {
		bHandled = FALSE;	// (WTL's: the command's string resource, if it has one)
		return 0;
	}
	// a toolbar button's tip is its menu item's text: "Step &Into\tF11" -> "Step Into (F11)"
	if (int tab = text.Find(L'\t'); tab >= 0)
		text = text.Left(tab) + L" (" + text.Mid(tab + 1) + L")";
	text.Replace(L"&&", L"\x1");
	text.Remove(L'&');
	text.Replace(L"\x1", L"&");
	wcsncpy_s(info->szText, text, _TRUNCATE);
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
	ApplyDefaultLayout(m_LayoutMode, true);
	return 0;
}

// (the other mode's default too: it becomes the layout of the mode in use, which does not change)
LRESULT CMainFrame::OnWindowDefaultLayout(WORD, WORD id, HWND, BOOL&) {
	ApplyDefaultLayout(id == ID_WINDOW_LAYOUT_KERNELDEFAULT ? LayoutMode::Kernel : LayoutMode::User, true);
	return 0;
}

LRESULT CMainFrame::OnWindowSaveLayout(WORD, WORD, HWND, BOOL&) {
	auto& store = m_Dock.Layouts();
	CInputDlg dlg(L"Save Layout", L"Layout name:");
	if (dlg.DoModal() != IDOK)
		return 0;

	std::wstring name(dlg.GetText());
	if (store.Contains(name)) {
		if (AtlMessageBox(*this, std::format(L"Replace the layout \"{}\"?", name).c_str(), IDS_TITLE, MB_YESNO | MB_ICONQUESTION) != IDYES)
			return 0;
	}
	else if (store.Count() >= ID_WINDOW_LAYOUT20 - ID_WINDOW_LAYOUT1 + 1) {
		AtlMessageBox(*this, L"There are as many saved layouts as there can be. Delete one first (Window > Layouts > Delete).", IDS_TITLE, MB_ICONWARNING);
		return 0;
	}
	m_Dock.SaveLayoutAs(name);
	SaveNamedLayouts();
	return 0;
}

LRESULT CMainFrame::OnWindowApplyLayout(WORD, WORD id, HWND, BOOL&) {
	auto names = m_Dock.Layouts().Names();
	const size_t index = id - ID_WINDOW_LAYOUT1;
	if (index >= names.size())
		return 0;

	std::wstring error;
	if (!m_Dock.ApplyLayout(names[index], &error))
		AtlMessageBox(*this, std::format(L"The layout \"{}\" could not be applied: {}", names[index], error).c_str(), IDS_TITLE, MB_ICONERROR);
	return 0;
}

LRESULT CMainFrame::OnWindowDeleteLayout(WORD, WORD id, HWND, BOOL&) {
	auto names = m_Dock.Layouts().Names();
	const size_t index = id - ID_WINDOW_DELETELAYOUT1;
	if (index >= names.size())
		return 0;

	if (AtlMessageBox(*this, std::format(L"Delete the layout \"{}\"?", names[index]).c_str(), IDS_TITLE, MB_YESNO | MB_ICONQUESTION) == IDYES) {
		m_Dock.Layouts().Remove(names[index]);
		SaveNamedLayouts();
	}
	return 0;
}

// Window > Layouts: the two defaults (in the menu resource), then the saved layouts and a submenu to delete them
void CMainFrame::FillLayoutsMenu(CMenuHandle menu) {
	while (menu.GetMenuItemCount() > 2)
		menu.DeleteMenu(2, MF_BYPOSITION);

	auto names = m_Dock.Layouts().Names();
	if (names.empty())
		return;

	menu.AppendMenu(MF_SEPARATOR);
	CMenuHandle remove;
	remove.CreatePopupMenu();
	for (size_t i = 0; i < names.size() && i <= ID_WINDOW_LAYOUT20 - ID_WINDOW_LAYOUT1; i++) {
		// (a '&' in a name is not an accelerator)
		std::wstring text;
		for (auto ch : names[i])
			text += ch == L'&' ? std::wstring(L"&&") : std::wstring(1, ch);
		menu.AppendMenu(MF_STRING, ID_WINDOW_LAYOUT1 + static_cast<UINT>(i), text.c_str());
		remove.AppendMenu(MF_STRING, ID_WINDOW_DELETELAYOUT1 + static_cast<UINT>(i), text.c_str());
	}
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_POPUP, remove, L"&Delete");
}

bool CMainFrame::ApplyDefaultLayout(LayoutMode mode, bool report) {
	std::wstring error;
	if (m_Dock.LoadState(BuildDefaultLayout(mode, m_Dock.Layout().Dpi()), {}, &error, false))
		return true;

	ATLTRACE(L"Default layout not applied: %ws\n", error.c_str());
	if (report)
		AtlMessageBox(*this, error.c_str(), IDS_TITLE, MB_ICONERROR);
	return false;
}

// Each mode keeps its layout as the user left it: the one in use goes into m_ModeLayouts, and the other comes out
// (or its default, the first time).
void CMainFrame::SwitchLayoutMode(LayoutMode mode) {
	if (mode == m_LayoutMode)
		return;

	m_ModeLayouts.Set(LayoutModeName(m_LayoutMode), m_Dock.Layout().Save());
	m_LayoutMode = mode;
	AppSettings::Get().LayoutMode(static_cast<int>(mode));

	std::wstring error;
	auto text = m_ModeLayouts.Find(LayoutModeName(mode));
	if (!text || !m_Dock.LoadState(*text, CurrentLayoutOptions(), &error, false))
		ApplyDefaultLayout(mode, false);
}

void CMainFrame::SaveNamedLayouts() {
	if (auto path = GetDataPath(L"layouts.json"); !path.empty())
		m_Dock.Layouts().SaveToFile(path);
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
	if (!m_ProcessServer.IsEmpty()) {
		AttachOnServer();
		return 0;
	}

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
	if (m_Connecting)
		CancelKernelConnection();
	else if (session.IsRunning() || session.IsBusy())
		DebugManager::Debugger().Break();

	return 0;
}

LRESULT CMainFrame::OnDebugGo(WORD, WORD, HWND, BOOL&) {
	if (DebugManager::Session().CanRun())
		DebugManager::Debugger().Go();

	return 0;
}

LRESULT CMainFrame::OnDebugStop(WORD, WORD, HWND, BOOL&) {
	if (m_Connecting)
		CancelKernelConnection();
	else if (DebugManager::Session().HasTarget())
		DebugManager::Debugger().Stop();

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
	auto& dbg = DebugManager::Debugger();
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
		if (FAILED(co_await DebugManager::Debugger().AddBreakpoint(bp)))
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

void CMainFrame::OnTargetAttached() {
	const auto target = DebugManager::Session().Target();
	if (target != TargetType::Unknown)
		SwitchLayoutMode((target & TargetType::Kernel) == TargetType::Kernel ? LayoutMode::Kernel : LayoutMode::User);
}

void CMainFrame::OnTargetEnded() {
	SetTargetName({});
	m_BreakpointsKey.clear();
	m_AnalyzeOnBreak = false;
}

void CMainFrame::OnException(const ExceptionData& data) {
	if (!data.FirstChance)
		m_AnalyzeOnBreak = true;
}

void CMainFrame::OnBroke() {
	// the target stopped (a breakpoint, an exception, Break) while the user was in another program: the taskbar button
	// flashes until DebugX is brought to the front (not for a step, which the user is in front for)
	auto foreground = ::GetForegroundWindow();
	if (foreground != m_hWnd && ::GetAncestor(foreground, GA_ROOTOWNER) != m_hWnd) {
		FLASHWINFO flash{ sizeof(flash), m_hWnd, FLASHW_TRAY | FLASHW_TIMERNOFG };
		::FlashWindowEx(&flash);
	}
	if (m_AnalyzeOnBreak) {
		m_AnalyzeOnBreak = false;
		PostMessage(WM_COMMAND, ID_DEBUG_ANALYZE);
	}
}

LRESULT CMainFrame::OnScript(WORD, WORD id, HWND, BOOL&) {
	auto& session = DebugManager::Session();
	if (session.IsRunning() || session.IsBusy()) {
		::MessageBeep(MB_ICONWARNING);
		return 0;
	}
	if (id == ID_DEBUG_UNLOADSCRIPT) {
		UnloadScript();
		return 0;
	}
	CFileDialog dlg(TRUE, L"js", nullptr, OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ENABLESIZING,
		L"JavaScript (*.js)\0*.js\0All Files\0*.*\0", *this);
	// the system's file picker does not take the dark mode hook
	WTLHelper::SuspendHook();
	auto ok = dlg.DoModal() == IDOK;
	WTLHelper::ResumeHook();
	if (ok)
		RunScript(id == ID_DEBUG_RUNSCRIPT ? IDebugger::ScriptAction::Run : IDebugger::ScriptAction::Load, dlg.m_szFileName);
	return 0;
}

Task<void> CMainFrame::RunScript(IDebugger::ScriptAction action, std::wstring path) {
	auto alive = m_Lifetime.Watch();
	// (what it says goes to the Command view too)
	std::wstring output;
	auto hr = co_await DebugManager::Debugger().Script(action, path.c_str(), output);
	if (!*alive || SUCCEEDED(hr))
		co_return;
	std::wstring text = hr == E_NOTIMPL
		? L"Scripts need the Windows Kits debugger's script providers (JsProvider.dll); the engine in use (" + DebugManager::EnginePath() + L") has none."
		: L"The script failed:\n\n" + (output.size() > 2000 ? output.substr(0, 2000) + L"..." : output);
	AtlMessageBox(*this, text.c_str(), IDS_TITLE, MB_ICONERROR);
}

Task<void> CMainFrame::UnloadScript() {
	auto alive = m_Lifetime.Watch();
	std::vector<std::wstring> scripts;
	co_await DebugManager::Debugger().EnumScripts(scripts);
	if (!*alive)
		co_return;
	if (scripts.empty()) {
		AtlMessageBox(*this, L"No script is loaded.", IDS_TITLE, MB_ICONINFORMATION);
		co_return;
	}
	// which one: a menu of them, at the mouse
	CMenu menu;
	menu.CreatePopupMenu();
	for (size_t i = 0; i < scripts.size(); i++)
		menu.AppendMenu(MF_STRING, static_cast<UINT>(i + 1), scripts[i].c_str());
	if (scripts.size() > 1) {
		menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(MF_STRING, static_cast<UINT>(scripts.size() + 1), L"&All of Them");
	}
	CPoint pt;
	::GetCursorPos(&pt);
	auto cmd = static_cast<size_t>(menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd));
	if (cmd == 0)
		co_return;
	std::wstring output;
	for (size_t i = 0; i < scripts.size(); i++) {
		if (cmd == i + 1 || cmd == scripts.size() + 1) {
			co_await DebugManager::Debugger().Script(IDebugger::ScriptAction::Unload, scripts[i].c_str(), output);
			if (!*alive)
				co_return;
		}
	}
}

LRESULT CMainFrame::OnConnectServer(WORD, WORD, HWND, BOOL&) {
	if (!CanStartSession())
		return 0;
	CInputDlg dlg(L"Connect to Process Server",
		L"How dbgsrv was started on the other machine (dbgsrv -t tcp:port=5005): tcp:server=host,port=5005, or npipe:server=host,pipe=name",
		AppSettings::Get().ProcessServer().c_str());
	if (dlg.DoModal(m_hWnd) == IDOK && !dlg.GetText().IsEmpty())
		ConnectServer(dlg.GetText());
	return 0;
}

Task<void> CMainFrame::ConnectServer(CString options) {
	auto alive = m_Lifetime.Watch();
	::SetWindowText(m_hWndStatusBar, (L"Connecting to " + options + L"...").GetString());
	auto hr = co_await DebugManager::Debugger().ConnectProcessServer(options);
	if (!*alive)
		co_return;
	if (FAILED(hr)) {
		::SetWindowText(m_hWndStatusBar, L"Ready");
		CString text;
		text.Format(L"Failed to connect to the process server \"%s\" (0x%08X)", (PCWSTR)options, static_cast<unsigned>(hr));
		AtlMessageBox(*this, (PCWSTR)text, IDS_TITLE, MB_ICONERROR);
		co_return;
	}
	m_ProcessServer = options;
	AppSettings::Get().ProcessServer((PCWSTR)options);
	::SetWindowText(m_hWndStatusBar, (L"Process server: " + options + L" (Attach to Process and Run Executable are on that machine)").GetString());
	UpdateUI();
}

LRESULT CMainFrame::OnDisconnectServer(WORD, WORD, HWND, BOOL&) {
	if (CanStartSession() && !m_ProcessServer.IsEmpty())
		DisconnectServer();
	return 0;
}

Task<void> CMainFrame::DisconnectServer() {
	auto alive = m_Lifetime.Watch();
	co_await DebugManager::Debugger().DisconnectProcessServer();
	if (!*alive)
		co_return;
	m_ProcessServer.Empty();
	::SetWindowText(m_hWndStatusBar, L"Ready");
	UpdateUI();
}

Task<void> CMainFrame::AttachOnServer() {
	auto alive = m_Lifetime.Watch();
	std::vector<ServerProcess> processes;
	auto hr = co_await DebugManager::Debugger().EnumServerProcesses(processes);
	if (!*alive)
		co_return;
	if (FAILED(hr)) {
		CString text;
		text.Format(L"The processes of %s cannot be listed (0x%08X)", (PCWSTR)m_ProcessServer, static_cast<unsigned>(hr));
		AtlMessageBox(*this, (PCWSTR)text, IDS_TITLE, MB_ICONERROR);
		co_return;
	}
	std::vector<std::pair<DWORD, CString>> list;
	for (auto& p : processes)
		list.push_back({ p.Pid, p.Name.c_str() });
	CProcessSelectDlg dlg;
	dlg.SetRemoteProcesses(m_ProcessServer, std::move(list));
	if (dlg.DoModal() == IDOK) {
		CString name;
		auto pid = dlg.GetSelectedProcess(name);
		AttachToProcess(pid, name);
	}
}

LRESULT CMainFrame::OnDebugAnalyze(WORD, WORD, HWND, BOOL&) {
	auto& session = DebugManager::Session();
	if (!session.HasTarget() || session.IsRunning()) {
		::MessageBeep(MB_ICONWARNING);
		return 0;
	}
	// the Analysis pane shown (made, the first time), then told
	BOOL handled;
	OnViewPane(0, ID_VIEW_ANALYSIS, nullptr, handled);
	if (auto pane = m_Dock.Layout().FindPane(L"analysis"); pane && pane->hWnd)
		::SendMessage(pane->hWnd, WM_COMMAND, ID_DEBUG_ANALYZE, 0);
	return 0;
}

Task<void> CMainFrame::SaveBreakpoints() {
	auto alive = m_Lifetime.Watch();
	auto key = m_BreakpointsKey;
	std::vector<BreakpointInfo> breakpoints;
	auto hr = co_await DebugManager::Debugger().EnumBreakpoints(breakpoints);
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
		DebugManager::Debugger().Step(kind, ActivePaneId() != L"disassembly");
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
		// a list pane with a filter box takes it there (CListPane::OnEditFind)
		auto pane = m_Dock.ActivePane();
		if (!pane || !pane->hWnd || !::SendMessage(pane->hWnd, WM_COMMAND, ID_EDIT_FIND, 0))
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
		DebugManager::Debugger().ReloadSymbols();
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
	auto& dbg = DebugManager::Debugger();

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
		DebugManager::Debugger().SetSymbolPath(settings.SymbolPath().c_str());
	if (!settings.SourcePath().empty())
		DebugManager::Debugger().SetSourcePath(settings.SourcePath().c_str());
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
	auto& dbg = DebugManager::Debugger();

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

	auto& dbg = DebugManager::Debugger();
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

LRESULT CMainFrame::OnDebugShowNext(WORD, WORD, HWND, BOOL&) {
	if (DebugManager::Session().IsBroken())
		ShowNextStatement();
	else
		::MessageBeep(MB_ICONWARNING);
	return 0;
}

Task<void> CMainFrame::ShowNextStatement() {
	auto alive = m_Lifetime.Watch();
	auto& session = DebugManager::Session();
	auto& dbg = DebugManager::Debugger();
	// (from the Disassembly window: there; otherwise the source file, if there is one)
	const bool inDisassembly = ActivePaneId() == L"disassembly";

	// back to the thread's own frame (the panes that follow the scope come back with it)
	if (session.CurrentFrame() != 0) {
		co_await dbg.SetScopeFrame(0);
		if (!*alive)
			co_return;
	}
	if (!inDisassembly) {
		ULONG64 ip = 0;
		auto hr = co_await dbg.GetInstructionOffset(ip);
		if (!*alive || FAILED(hr))
			co_return;
		std::vector<SourceLocation> locations;
		co_await dbg.GetSourceLocations({ ip }, locations);
		if (!*alive)
			co_return;
		if (locations.size() == 1 && !locations[0].Path.empty()) {
			if (auto view = m_Sources.Open(locations[0].Path, locations[0].Line, true))
				view->SetSymbolFile(locations[0].File);
			co_return;
		}
	}
	// no source there: the instruction (an empty address bar goes back to it)
	SendMessage(WM_GOTODISASSEMBLY, 0, reinterpret_cast<LPARAM>(L""));
}

LRESULT CMainFrame::OnDeleteAllBreakpoints(WORD, WORD, HWND, BOOL&) {
	DebugManager::RemoveAllBreakpoints(m_hWnd);
	return 0;
}

LRESULT CMainFrame::OnEnableAllBreakpoints(WORD, WORD id, HWND, BOOL&) {
	DebugManager::SetAllBreakpointsEnabled(id == ID_DEBUG_ENABLEALLBREAKPOINTS);
	return 0;
}

Task<void> CMainFrame::RunExecutable(CString exePath, CString arguments, CString workingDir) {
	auto alive = m_Lifetime.Watch();
	auto& dbg = DebugManager::Debugger();

	auto hr = co_await dbg.LaunchAndAttach(exePath, arguments, workingDir);
	if (!*alive)
		co_return;

	if (FAILED(hr)) {
		AtlMessageBox(*this, L"Failed to launch process", IDS_TITLE, MB_ICONERROR);
		co_return;
	}
	m_Launch = Launch{ exePath, arguments, workingDir };
	// (a path on a process server's machine is not one to run again here, nor to keep breakpoints by)
	const bool remote = !m_ProcessServer.IsEmpty();
	if (!remote)
		RecentTargets::Add({ RecentTarget::Kind::Run, (PCWSTR)exePath, (PCWSTR)arguments, (PCWSTR)workingDir });
	SetTargetName(::PathFindFileName(exePath));
	UpdateUI();
	auto failed = co_await RestoreBreakpoints(remote ? std::wstring() : SavedBreakpoints::KeyOf((PCWSTR)exePath));
	if (!*alive)
		co_return;
	co_await dbg.OutputPrompt();
	if (*alive)
		ReportRestoreFailures(failed);
}

Task<void> CMainFrame::AttachToProcess(DWORD pid, CString name) {
	auto alive = m_Lifetime.Watch();
	auto& dbg = DebugManager::Debugger();

	m_Launch.reset();		// (no Restart for it)
	// (whose breakpoints: asked now, while the process is there; a process server's are not kept)
	const bool remote = !m_ProcessServer.IsEmpty();
	auto key = remote ? std::wstring() : SavedBreakpoints::KeyOf(SavedBreakpoints::ImagePathOf(pid));
	auto hr = co_await dbg.AttachToProcess(pid);
	if (!*alive)
		co_return;

	if (FAILED(hr)) {
		CString text;
		text.Format(L"Failed to attach to PID %u", pid);
		AtlMessageBox(*this, (PCWSTR)text, IDS_TITLE, MB_ICONERROR);
		co_return;
	}
	if (!name.IsEmpty() && !remote)
		RecentTargets::Add({ RecentTarget::Kind::Attach, (PCWSTR)name });
	SetTargetName(std::format(L"{} ({})", name.IsEmpty() ? L"PID" : (PCWSTR)name, pid).c_str());
	auto failed = co_await RestoreBreakpoints(key);
	if (!*alive)
		co_return;
	co_await dbg.OutputPrompt();
	if (*alive)
		ReportRestoreFailures(failed);
}

Task<void> CMainFrame::AttachLocalKernel() {
	auto alive = m_Lifetime.Watch();
	auto& dbg = DebugManager::Debugger();

	m_Launch.reset();		// (no Restart for it)
	auto hr = co_await dbg.AttachLocalKernel();
	if (!*alive)
		co_return;

	if (FAILED(hr)) {
		AtlMessageBox(*this, L"Failed to attach to the local kernel", IDS_TITLE, MB_ICONERROR);
		co_return;
	}
	RecentTargets::Add({ RecentTarget::Kind::LocalKernel });
	SetTargetName(L"Local kernel");
	co_await dbg.OutputPrompt();
}

Task<void> CMainFrame::AttachKernel(CString connection) {
	auto alive = m_Lifetime.Watch();
	auto& dbg = DebugManager::Debugger();

	m_Launch.reset();		// (no Restart for it)
	// (until the target connects -- with no machine there, for ever; Break or Stop gives up, CancelKernelConnection)
	m_Connecting = true;
	UpdateUI();
	::SetWindowText(m_hWndStatusBar, (L"Waiting for the kernel to connect (" + connection + L")... Stop gives up").GetString());
	auto hr = co_await dbg.AttachKernel(connection);
	if (!*alive)
		co_return;

	m_Connecting = false;
	UpdateUI();
	::SetWindowText(m_hWndStatusBar, L"Ready");
	if (FAILED(hr)) {
		CString text;
		text.Format(L"Failed to attach to the kernel with \"%s\" (0x%08X)", (PCWSTR)connection, static_cast<unsigned>(hr));
		AtlMessageBox(*this, (PCWSTR)text, IDS_TITLE, MB_ICONERROR);
		co_return;
	}
	RecentTargets::Add({ RecentTarget::Kind::Kernel, (PCWSTR)connection });
	SetTargetName(L"Kernel (" + connection + L")");
	co_await dbg.OutputPrompt();
}

Task<void> CMainFrame::OpenDumpFile(CString path) {
	auto alive = m_Lifetime.Watch();
	auto& dbg = DebugManager::Debugger();

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
	SetTargetName(::PathFindFileName(path));
	co_await dbg.OutputPrompt();
	// (what a dump is about)
	if (*alive)
		PostMessage(WM_COMMAND, ID_DEBUG_ANALYZE);
}

LRESULT CMainFrame::OnInitMenuPopup(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	// (the rest of the menus are CAutoUpdateUI's)
	bHandled = FALSE;
	CMenuHandle menu(reinterpret_cast<HMENU>(wParam));
	auto first = menu.GetMenuItemID(0);
	if (first == ID_WINDOW_LAYOUT_USERDEFAULT) {
		FillLayoutsMenu(menu);
		return 0;
	}
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

LRESULT CMainFrame::OnDropFiles(UINT, WPARAM wParam, LPARAM, BOOL&) {
	auto drop = reinterpret_cast<HDROP>(wParam);
	std::vector<std::wstring> files;
	const UINT count = ::DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
	for (UINT i = 0; i < count; i++) {
		WCHAR path[MAX_PATH];
		if (::DragQueryFileW(drop, i, path, _countof(path)))
			files.push_back(path);
	}
	::DragFinish(drop);
	// (the dialog a program asks for comes up in front)
	::SetForegroundWindow(m_hWnd);

	auto extension = [](const std::wstring& path) {
		auto dot = path.find_last_of(L'.');
		auto ext = dot == std::wstring::npos ? std::wstring() : path.substr(dot);
		for (auto& c : ext)
			c = towlower(c);
		return ext;
		};
	for (auto& file : files) {
		auto ext = extension(file);
		if (ext == L".dmp" || ext == L".mdmp" || ext == L".hdmp" || ext == L".exe") {
			// a target: one, and only when nothing is being debugged
			if (files.size() > 1 || !CanStartSession()) {
				::MessageBeep(MB_ICONWARNING);
				continue;
			}
			if (ext == L".exe") {
				// (the arguments are asked for)
				CRunExeDlg dlg(file.c_str());
				if (dlg.DoModal() == IDOK)
					RunExecutable(dlg.GetExePath(), dlg.GetArguments(), dlg.GetWorkingDir());
			}
			else
				OpenDumpFile(file.c_str());
		}
		else if (::GetFileAttributesW(file.c_str()) & FILE_ATTRIBUTE_DIRECTORY)
			::MessageBeep(MB_ICONWARNING);
		else
			// anything else is a file to read: a source file
			m_Sources.Open(file, 0, true);
	}
	return 0;
}

Task<void> CMainFrame::AttachToProcessByName(std::wstring name) {
	// the running process with that name, if there is just one
	std::vector<DWORD> pids;
	wil::unique_handle snapshot(::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
	PROCESSENTRY32W pe{ sizeof(pe) };
	if (snapshot && ::Process32FirstW(snapshot.get(), &pe)) {
		do {
			if (_wcsicmp(pe.szExeFile, name.c_str()) == 0 && pe.th32ProcessID != ::GetCurrentProcessId())
				pids.push_back(pe.th32ProcessID);
		} while (::Process32NextW(snapshot.get(), &pe));
	}
	if (pids.size() == 1) {
		co_await AttachToProcess(pids[0], name.c_str());
		co_return;
	}
	auto text = pids.empty() ? std::format(L"No process named {} is running.", name)
		: std::format(L"{} processes named {} are running: choose one with Attach to Process.", pids.size(), name);
	AtlMessageBox(*this, text.c_str(), IDS_TITLE, MB_ICONINFORMATION);
}

Task<void> CMainFrame::StartFromCommandLine(CommandLine cmd) {
	auto alive = m_Lifetime.Watch();
	// (a relative path is the current directory's: made full, for Recent Targets and the saved breakpoints)
	auto fullPath = [](const std::wstring& path) {
		WCHAR full[MAX_PATH];
		return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES && ::GetFullPathNameW(path.c_str(), _countof(full), full, nullptr)
			? std::wstring(full) : path;
		};

	switch (cmd.Type) {
		case CommandLine::Kind::None:
			co_return;

		case CommandLine::Kind::Help: {
			auto text = cmd.Error.empty() ? std::wstring(CommandLine::Usage()) : cmd.Error + L"\n\n" + CommandLine::Usage();
			AtlMessageBox(*this, text.c_str(), IDS_TITLE, cmd.Error.empty() ? MB_ICONINFORMATION : MB_ICONWARNING);
			co_return;
		}

		case CommandLine::Kind::Run:
			co_await RunExecutable(fullPath(cmd.Path).c_str(), cmd.Arguments.c_str(), L"");
			break;

		case CommandLine::Kind::Attach: {
			auto image = SavedBreakpoints::ImagePathOf(cmd.Pid);
			co_await AttachToProcess(cmd.Pid, image.empty() ? CString() : ::PathFindFileName(image.c_str()));
			// AeDebug's event: the process that crashed waits for it -- signaled whether the attach worked or not
			if (cmd.Event) {
				auto event = reinterpret_cast<HANDLE>(cmd.Event);
				::SetEvent(event);
				::CloseHandle(event);
			}
			break;
		}

		case CommandLine::Kind::AttachByName:
			co_await AttachToProcessByName(cmd.Path);
			break;

		case CommandLine::Kind::Dump:
			co_await OpenDumpFile(fullPath(cmd.Path).c_str());
			break;

		case CommandLine::Kind::Kernel:
			co_await AttachKernel(cmd.Path.c_str());
			break;

		case CommandLine::Kind::LocalKernel:
			// (elevation is asked for there)
			PostMessage(WM_COMMAND, ID_DEBUG_ATTACHTOLOCALKERNEL);
			co_return;
	}
	if (!*alive)
		co_return;
	// -g: past the initial break. (Not CanRun: the session still says busy as the start's last task completes -- the
	// notice comes after -- and Go is queued behind it anyway.)
	auto& session = DebugManager::Session();
	if (cmd.Go && session.HasTarget() && !session.IsRunning() && (session.Target() & TargetType::Dump) != TargetType::Dump)
		DebugManager::Debugger().Go();
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
			AttachToProcessByName(target.Path);
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
	auto& dbg = DebugManager::Debugger();
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

LRESULT CMainFrame::OnGoToDisassembly(UINT, WPARAM, LPARAM lParam, BOOL&) {
	BOOL handled;
	OnViewPane(0, ID_VIEW_DISASSEMBLY, nullptr, handled);
	if (auto pane = m_Dock.Layout().FindPane(L"disassembly"); pane && pane->hWnd)
		::SendMessage(pane->hWnd, WM_GOTODISASSEMBLY, 0, lParam);
	return 0;
}

Task<void> CMainFrame::AddBreakpoint(BreakpointSettings settings) {
	auto alive = m_Lifetime.Watch();

	auto hr = co_await DebugManager::Debugger().AddBreakpoint(settings);
	if (!*alive)
		co_return;

	if (FAILED(hr))
		AtlMessageBox(*this, (PCWSTR)BreakpointErrorText(settings, hr), IDS_TITLE, MB_ICONERROR);
	// (the list of breakpoints hears of it from the engine)
}

void CMainFrame::InitMenu(HMENU menu) {
	// (kept for the life of the process, as WTLHelper::InitMenu wants an icon that is not a resource)
	static HICON shield = IconHelper::GetShieldIcon();
	MenuItemData commands[] = {
		{ ID_FILE_RUNASADMINISTRATOR, 0, shield },
		{ ID_EDIT_CUT, IDI_CUT },
		{ ID_EDIT_COPY, IDI_COPY },
		{ ID_EDIT_PASTE, IDI_PASTE },
		{ ID_VIEW_REFRESH, IDI_REFRESH },
		{ ID_VIEW_COMMAND, IDI_COMMAND },
		{ ID_VIEW_THREADS, IDI_THREADS },
		{ ID_VIEW_PROCESSES, IDI_PROCESSES },
		{ ID_VIEW_KERNELTHREADS, IDI_THREADS },
		{ ID_VIEW_USERPROCESSES, IDI_PROCESSES },
		{ ID_VIEW_HANDLES, IDI_TOOL },
		{ ID_VIEW_DRIVERS, IDI_DLL },
		{ ID_VIEW_OBJECTS, IDI_STRUCT },
		{ ID_VIEW_IRPS, IDI_EVENT },
		{ ID_VIEW_ANALYSIS, IDI_FIND },
		{ ID_VIEW_MEMORY2, IDI_MEMORY },
		{ ID_VIEW_MEMORY3, IDI_MEMORY },
		{ ID_VIEW_MEMORY4, IDI_MEMORY },
		{ ID_VIEW_OUTPUT, IDI_OUTPUT },
		{ ID_VIEW_MODULES, IDI_DLL },
		{ ID_VIEW_REGISTERS, IDI_CPU },
		{ ID_DEBUG_ADDBREAKPOINT, IDI_BREAKPOINT },
		{ ID_VIEW_CALLSTACK, IDI_CALLSTACK },
		{ ID_VIEW_BREAKPOINTS, IDI_BREAKPOINTS },
		{ ID_VIEW_MEMORY, IDI_MEMORY },
		{ ID_VIEW_DISASSEMBLY, IDI_STRUCT },
		{ ID_VIEW_LOCALS, IDI_LOCALS },
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
		{ ID_DEBUG_SHOWNEXT, IDI_ICON9 },
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

	layout.SetAppVersion(LayoutVersion);
	m_LayoutMode = AppSettings::Get().LayoutMode() == 1 ? LayoutMode::Kernel : LayoutMode::User;
	if (auto path = GetDataPath(L"mode-layouts.json"); FileExists(path))
		m_ModeLayouts.LoadFromFile(path);
	if (auto path = GetDataPath(L"layouts.json"); FileExists(path))
		m_Dock.Layouts().LoadFromFile(path);

	// whatever the last run left, if it did; else (the first run) the default layout of the mode
	bool restored = false;
	if (auto path = GetDataPath(L"layout.json"); FileExists(path)) {
		std::wstring error;
		restored = m_Dock.LoadStateFromFile(path, CurrentLayoutOptions(), &error);
		if (!restored)
			ATLTRACE(L"Layout not restored: %ws\n", error.c_str());
	}
	if (!restored && !ApplyDefaultLayout(m_LayoutMode, false))
		layout.Show(layout.FindPane(L"command"));
}

void CMainFrame::SaveLayout() {
	if (auto path = GetDataPath(L"layout.json"); !path.empty())
		m_Dock.SaveStateToFile(path);
	// (the mode in use is kept there too)
	m_ModeLayouts.Set(LayoutModeName(m_LayoutMode), m_Dock.Layout().Save());
	if (auto path = GetDataPath(L"mode-layouts.json"); !path.empty())
		m_ModeLayouts.SaveToFile(path);
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
	UIEnable(ID_DEBUG_CONNECTSERVER, canStart, force);
	UIEnable(ID_DEBUG_DISCONNECTSERVER, canStart && !m_ProcessServer.IsEmpty(), force);
	const bool canScript = !session.IsRunning() && !session.IsBusy();
	UIEnable(ID_DEBUG_RUNSCRIPT, canScript, force);
	UIEnable(ID_DEBUG_LOADSCRIPT, canScript, force);
	UIEnable(ID_DEBUG_UNLOADSCRIPT, canScript, force);
	UIEnable(ID_DEBUG_ATTACHTOKERNEL, canStart, force);

	UIEnable(ID_DEBUG_GO, canRun, force);
	UIEnable(ID_DEBUG_BREAK, session.IsRunning() || session.IsBusy(), force);
	UIEnable(ID_DEBUG_STOP, attached || m_Connecting, force);
	UIEnable(ID_DEBUG_DETACH, session.HasTarget() && !isDump, force);
	UIEnable(ID_DEBUG_SAVEDUMP, (target & TargetType::Live) == TargetType::Live && session.IsBroken() && !session.IsBusy(), force);
	UIEnable(ID_DEBUG_ANALYZE, session.HasTarget() && !session.IsRunning() && !session.IsBusy(), force);
	// (Recent Targets is made when it opens, OnInitMenuPopup; its first item is in the menu resource, so it is CAutoUpdateUI's
	// as well, which would otherwise enable it)
	UIEnable(ID_FILE_RECENT1, canStart && !RecentTargets::Load().empty(), force);
	UIEnable(ID_VIEW_REFRESH, session.HasTarget(), force);
	UIEnable(ID_DEBUG_STEPOVER, canRun, force);
	UIEnable(ID_DEBUG_STEPINTO, canRun, force);
	UIEnable(ID_DEBUG_STEPOUT, canRun, force);
	UIEnable(ID_DEBUG_RUNTOCURSOR, canRun, force);
	UIEnable(ID_DEBUG_SETNEXTSTATEMENT, canRun, force);
	UIEnable(ID_DEBUG_SHOWNEXT, session.IsBroken(), force);
	const bool canChangeBreakpoints = DebugManager::CanChangeBreakpoints();
	UIEnable(ID_DEBUG_DELETEALLBREAKPOINTS, canChangeBreakpoints, force);
	UIEnable(ID_DEBUG_ENABLEALLBREAKPOINTS, canChangeBreakpoints, force);
	UIEnable(ID_DEBUG_DISABLEALLBREAKPOINTS, canChangeBreakpoints, force);
	UIEnable(ID_DEBUG_RESTART, m_Launch.has_value() && !m_Restarting && session.HasTarget(), force);
	UIEnable(ID_DEBUG_RELOADSYMBOLS, session.HasTarget() && !session.IsRunning(), force);
	UIEnable(ID_OPTIONS_SYMBOLPATHS, !session.IsRunning(), force);

	// a pane of a target that is not there is not for showing
	for (auto& info : PaneRegistry::Get().Panes())
		UIEnable(info.Command, PaneRegistry::IsAvailable(info, target), force);
	UIEnable(ID_DEBUG_ADDBREAKPOINT, attached, force);
	UIEnable(ID_DEBUG_EXCEPTIONS, !session.IsRunning(), force);
	UpdateStatusParts();
}
