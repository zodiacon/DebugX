#include "stdafx.h"
#include "ScintillaCtrl.h"

// Scintilla's Win32 platform layer talks to the IME directly (ScintillaWin.cxx); not part of the
// vcpkg triplet's own libs, so not autolinked the way Scintilla.lib itself is.
#pragma comment(lib, "imm32.lib")

// The registration/teardown entry points a statically-linked Scintilla exports (the equivalent of what
// SciLexer.dll's DllMain would otherwise do); not declared in any installed header, so declared here.
extern "C" int Scintilla_RegisterClasses(void* hInstance);
extern "C" int Scintilla_ReleaseResources();

void RegisterScintillaClasses() {
	Scintilla_RegisterClasses(_AtlBaseModule.GetModuleInstance());
}

void UnregisterScintillaClasses() {
	Scintilla_ReleaseResources();
}

// explicit lengths throughout (not the null-terminated ATL CW2AEX/CA2WEX idiom): callers rely on the
// exact byte length of the result, e.g. to compute Scintilla style-run offsets
std::string ToUtf8(std::wstring_view text) {
	if (text.empty())
		return {};
	auto len = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
	std::string result(len, '\0');
	::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), len, nullptr, nullptr);
	return result;
}

std::wstring FromUtf8(std::string_view text) {
	if (text.empty())
		return {};
	auto len = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
	std::wstring result(len, L'\0');
	::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), len);
	return result;
}
