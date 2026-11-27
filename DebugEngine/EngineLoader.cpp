#include "pch.h"
#include "Debugger.h"
#include <appmodel.h>

using namespace DebugEngine;

namespace {
	// the folder of the debugger for this architecture, under the Windows Kits' Debuggers
	constexpr PCWSTR Architecture =
#if defined(_M_ARM64)
		L"arm64";
#elif defined(_M_X64)
		L"x64";
#else
		L"x86";
#endif

	bool FileExists(const std::wstring& path) {
		auto attributes = ::GetFileAttributesW(path.c_str());
		return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
	}

	std::wstring WithSlash(std::wstring path) {
		if (!path.empty() && path.back() != L'\\')
			path += L'\\';
		return path;
	}

	// a value of HKLM\SOFTWARE\Microsoft\Windows Kits\Installed Roots, in either registry view
	std::wstring InstalledRoot(PCWSTR name) {
		for (auto view : { KEY_WOW64_64KEY, KEY_WOW64_32KEY }) {
			HKEY key;
			if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows Kits\Installed Roots)", 0, KEY_QUERY_VALUE | view, &key) != ERROR_SUCCESS)
				continue;
			wchar_t value[MAX_PATH];
			DWORD size = sizeof(value);
			auto error = ::RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, value, &size);
			::RegCloseKey(key);
			if (error == ERROR_SUCCESS && *value)
				return WithSlash(value);
		}
		return {};
	}

	std::wstring ModulePath(HMODULE module) {
		wchar_t path[MAX_PATH * 2];
		return module && ::GetModuleFileNameW(module, path, _countof(path)) ? path : L"";
	}
}

std::wstring DebugEngine::WindowsKitsDebuggersFolder() {
	std::vector<std::wstring> candidates;
	// (the debuggers' own root, if registered, else under the kit's; else where the kits install by default)
	if (auto root = InstalledRoot(L"WindowsDebuggersRoot10"); !root.empty())
		candidates.push_back(root);
	if (auto root = InstalledRoot(L"KitsRoot10"); !root.empty())
		candidates.push_back(root + L"Debuggers\\");
	wchar_t programFiles[MAX_PATH];
	if (::ExpandEnvironmentStringsW(L"%ProgramFiles(x86)%", programFiles, _countof(programFiles)) && programFiles[0] != L'%')
		candidates.push_back(WithSlash(programFiles) + L"Windows Kits\\10\\Debuggers\\");

	for (auto& root : candidates) {
		auto folder = root + Architecture + L"\\";
		if (FileExists(folder + L"dbgeng.dll"))
			return folder;
	}
	return {};
}

std::wstring DebugEngine::WinDbgEngineFolder() {
	// (the folder under the WinDbg package: its own name for the architecture)
	constexpr PCWSTR PackageArchitecture =
#if defined(_M_ARM64)
		L"arm64";
#elif defined(_M_X64)
		L"amd64";
#else
		L"x86";
#endif
	UINT32 count = 0, bufferLength = 0;
	if (::GetPackagesByPackageFamily(L"Microsoft.WinDbg_8wekyb3d8bbwe", &count, nullptr, &bufferLength, nullptr) != ERROR_INSUFFICIENT_BUFFER || count == 0)
		return {};
	std::vector<PWSTR> names(count);
	std::vector<wchar_t> buffer(bufferLength);
	if (::GetPackagesByPackageFamily(L"Microsoft.WinDbg_8wekyb3d8bbwe", &count, names.data(), &bufferLength, buffer.data()) != ERROR_SUCCESS)
		return {};
	for (UINT32 i = 0; i < count; i++) {
		wchar_t path[MAX_PATH * 2];
		UINT32 length = _countof(path);
		if (::GetPackagePathByFullName(names[i], &length, path) != ERROR_SUCCESS)
			continue;
		auto folder = WithSlash(path) + PackageArchitecture + L"\\";
		if (FileExists(folder + L"dbgeng.dll"))
			return folder;
	}
	return {};
}

std::wstring DebugEngine::LoadEngine(EngineSource source, PCWSTR folder) {
	// (already loaded -- by an earlier call, or as an import: that one it is)
	if (auto loaded = ::GetModuleHandleW(L"dbgeng.dll"))
		return ModulePath(loaded);

	auto fromFolder = [](const std::wstring& folder) -> HMODULE {
		auto path = WithSlash(folder) + L"dbgeng.dll";
		// (its dependencies -- dbghelp.dll, dbgmodel.dll, dbgcore.dll -- from its own folder, not System32)
		return FileExists(path) ? ::LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH) : nullptr;
	};

	HMODULE engine = nullptr;
	switch (source) {
		case EngineSource::Automatic:
		case EngineSource::WindowsKits:
			if (auto kits = WindowsKitsDebuggersFolder(); !kits.empty())
				engine = fromFolder(kits);
			if (engine || source == EngineSource::WindowsKits)
				break;
			[[fallthrough]];
		case EngineSource::WinDbg:
			if (auto winDbg = WinDbgEngineFolder(); !winDbg.empty())
				engine = fromFolder(winDbg);
			break;
		case EngineSource::System32:
			engine = ::LoadLibraryExW(L"dbgeng.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
			break;
		case EngineSource::Folder:
			if (folder && *folder)
				engine = fromFolder(folder);
			break;
	}
	return ModulePath(engine);
}
