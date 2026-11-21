#include "pch.h"
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

std::string ToUtf8Text(std::string bytes) {
	if (bytes.size() >= 3 && static_cast<BYTE>(bytes[0]) == 0xEF && static_cast<BYTE>(bytes[1]) == 0xBB && static_cast<BYTE>(bytes[2]) == 0xBF)
		return bytes.substr(3);
	if (bytes.size() >= 2 && static_cast<BYTE>(bytes[0]) == 0xFF && static_cast<BYTE>(bytes[1]) == 0xFE) {
		std::wstring_view wide(reinterpret_cast<const wchar_t*>(bytes.data() + 2), (bytes.size() - 2) / sizeof(wchar_t));
		return ToUtf8(std::wstring(wide));
	}
	if (bytes.empty() || ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0) > 0)
		return bytes;

	std::wstring wide(::MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0), L'\0');
	::MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), wide.data(), static_cast<int>(wide.size()));
	return ToUtf8(wide);
}
