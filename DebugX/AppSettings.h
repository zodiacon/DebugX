#pragma once

#include <Settings.h>

// What the program remembers between runs, in HKCU\Software\DebugX\Settings (or in DebugX.ini next to the program, if
// there is one - see Settings::Load). Loaded at startup and saved when the main window is destroyed.
struct AppSettings : Settings {
	static constexpr PCWSTR RegistryKey = L"Software\\DebugX\\Settings";

	BEGIN_SETTINGS(AppSettings)
		SETTING(DarkMode, -1, SettingType::Int32);		// 1 dark, 0 light, -1 (never chosen): as the system is
		SETTING_STRING(PaneStates, L"");		// what the tool windows keep of themselves (PaneRegistry), a line for each
		SETTING(Dml, 1, SettingType::Int32);		// the engine's output in DML where it can be (Options > Debugger Markup Language)
		SETTING_STRING(KernelConnection, L"net:port=50000,key=");	// the last Attach to Kernel connection string
		SETTING_STRING(EventFilters, L"");		// Exception Settings: what is not as the engine has it (EventFilterText)
		// Options > Symbol and Source Paths, as the engine takes them (';'-separated); empty: never set, the engine's own
		// (_NT_SYMBOL_PATH, _NT_SOURCE_PATH) stand
		SETTING_STRING(SymbolPath, L"");
		SETTING_STRING(SourcePath, L"");
		SETTING_STRING(RecentTargets, L"");		// File > Recent Targets (RecentTargets.h), a line for each
		SETTING(DebugChildProcesses, 0, SettingType::Int32);	// Options > Debug Child Processes
		SETTING(HexDisplay, 0, SettingType::Int32);	// Options > Hexadecimal Display (Locals, Watch, the call tips)
		SETTING_STRING(ProcessServer, L"tcp:server=,port=5005");	// the last Connect to Process Server options
		SETTING(LayoutMode, 0, SettingType::Int32);	// the window layout in use (layout.json) is the one for: 0 user mode, 1 kernel mode
		// Options > Code Font / UI Font (AppFonts): "face;size in tenths of a point;weight;italic"; empty: the default
		SETTING_STRING(CodeFont, L"");
		SETTING_STRING(UiFont, L"");
	END_SETTINGS

	DEF_SETTING(DarkMode, int)
	DEF_SETTING_STRING(PaneStates)
	DEF_SETTING(Dml, int)
	DEF_SETTING_STRING(KernelConnection)
	DEF_SETTING_STRING(EventFilters)
	DEF_SETTING_STRING(SymbolPath)
	DEF_SETTING_STRING(SourcePath)
	DEF_SETTING_STRING(RecentTargets)
	DEF_SETTING(DebugChildProcesses, int)
	DEF_SETTING(HexDisplay, int)
	DEF_SETTING_STRING(ProcessServer)
	DEF_SETTING(LayoutMode, int)
	DEF_SETTING_STRING(CodeFont)
	DEF_SETTING_STRING(UiFont)
};
