//
// DebugEngineTests [suite...] [--engine system|package]
//
// Runs the DebugEngine tests (all suites, or the ones named) and returns the number of checks that failed. The engine
// is either System32's dbgeng.dll -- the one DebugX.exe loads -- or the newer one of the NuGet package (DebugEngine\amd64):
// they differ, so both are to be tested. Without --engine, the runner runs itself once with each and adds up.
//
#include "Harness.h"
#include <map>
#include <thread>
#include <atomic>

using namespace DebugEngine;

// (DebuggerX's Impl is a CComObject: there has to be an ATL module)
class CTestsModule : public ATL::CAtlModuleT<CTestsModule> {} _AtlModule;

Notifications g_Events;
static int g_Failures = 0;

void Check(bool ok, const char* what) {
	printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok)
		g_Failures++;
}

static std::wstring ExeDir() {
	wchar_t path[MAX_PATH];
	::GetModuleFileNameW(nullptr, path, _countof(path));
	std::wstring dir(path);
	return dir.substr(0, dir.find_last_of(L'\\') + 1);
}

std::wstring Target(PCWSTR name) {
	return ExeDir() + L"Tests\\" + name + L".exe";
}

namespace {
	struct Suite {
		const char* Name;
		Task<void>(*Run)(DebuggerX&);
	};
	const Suite Suites[] = {
		{ "scope", ScopeSuite },
		{ "bp", BreakpointSuite },
		{ "exc", ExceptionSuite },
		{ "src", SourceSuite },
		{ "next", NextStatementSuite },
		{ "dump", DumpSuite },
	};

	// dbgeng.dll is delay-loaded: whichever is loaded first is the one the engine uses
	bool LoadEngine(const std::wstring& which) {
		HMODULE engine = nullptr;
		if (which == L"system")
			engine = ::LoadLibraryExW(L"dbgeng.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		else if (which == L"package") {
			// (x64\<configuration>\ is two folders down from the solution's)
			auto path = ExeDir() + L"..\\..\\DebugEngine\\amd64\\dbgeng.dll";
			// (its dbghelp.dll and the others next to it, not System32's)
			engine = ::LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
		}
		if (!engine) {
			printf("cannot load the '%ls' dbgeng.dll (error %u)\n", which.c_str(), ::GetLastError());
			return false;
		}
		wchar_t path[MAX_PATH];
		::GetModuleFileNameW(engine, path, _countof(path));
		printf("engine: %ls\n", path);
		return true;
	}

	// the runner again, with --engine 'which' and the suites given; its exit code
	int RunChild(const std::wstring& which, const std::vector<std::wstring>& suites) {
		wchar_t exe[MAX_PATH];
		::GetModuleFileNameW(nullptr, exe, _countof(exe));
		std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --engine " + which;
		for (auto& s : suites)
			cmd += L" " + s;
		STARTUPINFOW si{ sizeof(si) };
		PROCESS_INFORMATION pi;
		if (!::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
			printf("cannot run %ls (error %u)\n", cmd.c_str(), ::GetLastError());
			return 1;
		}
		::WaitForSingleObject(pi.hProcess, INFINITE);
		DWORD code = 1;
		::GetExitCodeProcess(pi.hProcess, &code);
		::CloseHandle(pi.hThread);
		::CloseHandle(pi.hProcess);
		return static_cast<int>(code);
	}

	void RunSuite(const Suite& suite) {
		printf("==== %s\n", suite.Name);
		g_Events = {};
		DebuggerX dbg;
		auto hr = dbg.Create([](DebuggerNotificationType type, void* data) {
			switch (type) {
				case DebuggerNotificationType::Exception:
					g_Events.Exceptions.push_back(static_cast<ExceptionData*>(data)->Code);
					break;
				case DebuggerNotificationType::MemoryChanged:
					g_Events.MemoryChanged++;
					break;
				case DebuggerNotificationType::ContextChanged:
					g_Events.ContextChanged++;
					break;
				case DebuggerNotificationType::ScopeChanged:
					g_Events.Scopes.push_back(static_cast<ScopeData*>(data)->FrameIndex);
					break;
			}
			});
		if (FAILED(hr)) {
			Check(false, "DebuggerX::Create");
			return;
		}

		// (a suite that hangs -- a target that never stops -- ends the run rather than blocking it)
		std::atomic<bool> done = false;
		std::thread watchdog([&done, name = suite.Name] {
			for (int i = 0; i < 1800 && !done; i++)
				::Sleep(100);
			if (!done) {
				printf("FAIL: suite '%s' timed out\n", name);
				::ExitProcess(99);
			}
			});

		suite.Run(dbg);
		MSG msg;
		while (::GetMessage(&msg, nullptr, 0, 0)) {
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
		done = true;
		watchdog.join();
	}
}

int wmain(int argc, wchar_t* argv[]) {
	setvbuf(stdout, nullptr, _IONBF, 0);
	std::wstring engine;
	std::vector<std::wstring> names;
	for (int i = 1; i < argc; i++) {
		std::wstring arg(argv[i]);
		if (arg == L"--engine" && i + 1 < argc)
			engine = argv[++i];
		else if (arg == L"--help" || arg == L"-?" || arg == L"/?") {
			printf("DebugEngineTests [suite...] [--engine system|package]\nsuites:");
			for (auto& s : Suites)
				printf(" %s", s.Name);
			printf("\nWithout --engine: once with each engine. The exit code is the number of failed checks.\n");
			return 0;
		}
		else
			names.push_back(arg);
	}
	for (auto& name : names) {
		if (std::none_of(std::begin(Suites), std::end(Suites), [&](auto& s) { return std::wstring(CA2W(s.Name)) == name; })) {
			printf("no suite '%ls' (--help lists them)\n", name.c_str());
			return 1;
		}
	}

	if (engine.empty()) {
		int failures = 0;
		for (auto which : { L"system", L"package" }) {
			printf("######## engine: %ls\n", which);
			failures += RunChild(which, names);
		}
		printf("######## %d failure(s) in all\n", failures);
		return failures;
	}

	if (!LoadEngine(engine))
		return 1;
	::CoInitialize(nullptr);
	for (auto& suite : Suites) {
		if (names.empty() || std::find(names.begin(), names.end(), std::wstring(CA2W(suite.Name))) != names.end())
			RunSuite(suite);
	}
	::CoUninitialize();
	printf("%d failure(s)\n", g_Failures);
	return g_Failures;
}

// ---- helpers (Harness.h)

Task<std::vector<BreakpointInfo>> Breakpoints(DebuggerX& dbg) {
	std::vector<BreakpointInfo> bps;
	co_await dbg.EnumBreakpoints(bps);
	co_return bps;
}

Task<ULONG64> Eval(DebuggerX& dbg, PCWSTR expression) {
	ULONG64 value = ~0ull;
	co_await dbg.Evaluate(expression, value);
	co_return value;
}

Task<SourceLocation> Where(DebuggerX& dbg) {
	ULONG64 ip = 0;
	co_await dbg.GetInstructionOffset(ip);
	std::vector<SourceLocation> locations;
	co_await dbg.GetSourceLocations({ ip }, locations);
	auto location = locations.empty() ? SourceLocation{} : locations[0];
	auto slash = location.File.find_last_of(L'\\');
	printf("  at 0x%llX: %ls(%u)%s\n", ip, slash == std::wstring::npos ? location.File.c_str() : location.File.c_str() + slash + 1,
		location.Line, location.File.empty() || !location.Path.empty() ? "" : " (file not found)");
	co_return location;
}

Task<ULONG> CodeLevel(DebuggerX& dbg) {
	ULONG level = 99;
	co_await dbg.RunAsync([&](IDebugClient* client) -> HRESULT {
		CComQIPtr<IDebugControl> control(client);
		return control->GetCodeLevel(&level);
		});
	co_return level;
}

bool EndsWith(const std::wstring& s, const wchar_t* tail) {
	auto n = wcslen(tail);
	return s.size() >= n && _wcsicmp(s.c_str() + s.size() - n, tail) == 0;
}

bool Has(const std::vector<SymbolEntry>& entries, const wchar_t* name, ULONG depth) {
	return std::any_of(entries.begin(), entries.end(), [&](auto& e) { return e.Name == name && e.Depth == depth; });
}

void Dump(const char* title, const std::vector<SymbolEntry>& entries) {
	printf("  --- %s (%zu)\n", title, entries.size());
	for (auto& e : entries)
		printf("    %*s%s%ls = %ls  [%ls]%s\n", static_cast<int>(e.Depth) * 2, "", e.Expandable ? (e.Expanded ? "- " : "+ ") : "  ",
			e.Name.c_str(), e.Value.c_str(), e.Type.c_str(), e.Error ? " ERROR" : "");
}
