// DebugX.cpp : main source file for DebugX.exe
//

#include "pch.h"
#include "resource.h"
#include "aboutdlg.h"
#include "MainFrm.h"
#include "AppSettings.h"
#include "ScintillaCtrl.h"

CAppModule _Module;
AppSettings g_Settings;

int Run(LPTSTR /*lpstrCmdLine*/ = nullptr, int nCmdShow = SW_SHOWDEFAULT) {
	CMessageLoop theLoop;
	_Module.AddMessageLoop(&theLoop);

	auto frame = new CMainFrame;

	if (frame->CreateEx() == nullptr) {
		ATLTRACE(_T("Main window creation failed!\n"));
		return 0;
	}
	// (not in WM_CREATE: restoring the layout can show the frame, and it must not be shown before the dark-mode hook
	// has given it its menu bar subclass, which happens when WM_CREATE returns)
	frame->InitDock();

	frame->ShowWindow(nCmdShow);

	int nRet = theLoop.Run();

	_Module.RemoveMessageLoop();
	return nRet;
}

int WINAPI _tWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPTSTR lpstrCmdLine, int nCmdShow) {
	HRESULT hRes = ::CoInitialize(nullptr);
	ATLASSERT(SUCCEEDED(hRes));

	AtlInitCommonControls(ICC_COOL_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES);
	HINSTANCE hInstRich = ::LoadLibrary(CRichEditCtrl::GetLibraryName());

	hRes = _Module.Init(nullptr, hInstance);
	ATLASSERT(SUCCEEDED(hRes));

	// registers the "Scintilla" window class for the process, the same way loading the RichEdit DLL
	// above registers its class; CScintillaCtrl (any of them, anywhere) superclasses it from here on
	RegisterScintillaClasses();

	AppSettings::Get().Load(AppSettings::RegistryKey);
	// dark or not as the user last chose, and as the system is until then; before any window exists
	if (AppSettings::Get().DarkMode() < 0)
		WTLHelper::InitDarkMode();
	else
		WTLHelper::InitDarkMode(AppSettings::Get().DarkMode() ? DarkModeKind::Dark : DarkModeKind::Classic);

	int nRet = Run(lpstrCmdLine, nCmdShow);

	UnregisterScintillaClasses();
	::FreeLibrary(hInstRich);
	_Module.Term();
	::CoUninitialize();

	return nRet;
}
