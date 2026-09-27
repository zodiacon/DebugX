#pragma once

#include <Settings.h>

// What the program remembers between runs, in HKCU\Software\DebugX\Settings (or in DebugX.ini next to the program, if
// there is one - see Settings::Load). Loaded at startup and saved when the main window is destroyed.
struct AppSettings : Settings {
	static constexpr PCWSTR RegistryKey = L"Software\\DebugX\\Settings";

	BEGIN_SETTINGS(AppSettings)
		SETTING(DarkMode, -1, SettingType::Int32);		// 1 dark, 0 light, -1 (never chosen): as the system is
		SETTING_STRING(PaneStates, L"");		// what the tool windows keep of themselves (PaneRegistry), a line for each
	END_SETTINGS

	DEF_SETTING(DarkMode, int)
	DEF_SETTING_STRING(PaneStates)
};
