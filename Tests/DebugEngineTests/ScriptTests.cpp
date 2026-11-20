// Debugger scripts: JavaScript run, loaded, called through the data model, unloaded (loop.exe)
#include "Harness.h"
#include <fstream>

using namespace DebugEngine;

namespace {
	std::wstring WriteScript(const wchar_t* name, const char* text) {
		wchar_t temp[MAX_PATH];
		::GetTempPathW(_countof(temp), temp);
		auto path = std::format(L"{}DebugEngineTests-{}-{}", temp, ::GetCurrentProcessId(), name);
		std::ofstream(path, std::ios::binary) << text;
		return path;
	}
}

Task<void> ScriptSuite(DebuggerX& dbg) {
	auto good = WriteScript(L"good.js", R"(
"use strict";
function initializeScript() { return [new host.apiVersionSupport(1, 7)]; }
function invokeScript() { host.diagnostics.debugLog("Hello from the test script, g_iter " + host.evaluateExpression("g_iter") + "\n"); }
function twice(x) { return x * 2; }
)");
	auto bad = WriteScript(L"bad.js", R"(
"use strict";
function invokeScript() { throw new Error("the test script fails"); }
)");

	auto hr = co_await dbg.LaunchAndAttach(Target(L"loop").c_str());
	Check(SUCCEEDED(hr), "launch loop.exe");
	co_await dbg.AddBreakpoint(L"loop!Work");
	co_await dbg.ExecuteCommand(L"g");

	std::wstring output;
	hr = co_await dbg.Script(DebuggerX::ScriptAction::Run, good.c_str(), output);
	printf("  Run: 0x%08X: %ls\n", static_cast<unsigned>(hr), output.c_str());
	if (hr == E_NOTIMPL) {
		printf("SKIP: the engine has no script provider (JsProvider: the Windows Kits debugger's)\n");
	}
	else {
		Check(SUCCEEDED(hr) && output.find(L"Hello from the test script, g_iter 0") != std::wstring::npos, "Run: invokeScript ran, and read the target");
		// (.scriptrun leaves it loaded, as .scriptload would)
		std::vector<std::wstring> scripts;
		co_await dbg.EnumScripts(scripts);
		Check(scripts.size() == 1 && _wcsicmp(scripts[0].c_str(), good.c_str()) == 0, "EnumScripts: the script run, by its path (not the visualizers)");
		hr = co_await dbg.Script(DebuggerX::ScriptAction::Unload, good.c_str(), output);
		scripts.clear();
		co_await dbg.EnumScripts(scripts);
		Check(SUCCEEDED(hr) && scripts.empty(), "Unload");

		hr = co_await dbg.Script(DebuggerX::ScriptAction::Load, good.c_str(), output);
		Check(SUCCEEDED(hr) && output.find(L"Hello") == std::wstring::npos, "Load: loaded, invokeScript not run");
		co_await dbg.EnumScripts(scripts);
		Check(scripts.size() == 1, "EnumScripts: it");
		std::vector<SymbolEntry> watches;
		co_await dbg.EvaluateWatches(watches, { L"@$scriptContents.twice(21)" }, {});
		if (!watches.empty())
			printf("  @$scriptContents.twice(21) = %ls\n", watches[0].Value.c_str());
		Check(!watches.empty() && !watches[0].Error && (watches[0].Value == L"0x2a" || watches[0].Value == L"42"), "its function, called through the data model: 42 (0x2a)");

		hr = co_await dbg.Script(DebuggerX::ScriptAction::Unload, good.c_str(), output);
		scripts.clear();
		co_await dbg.EnumScripts(scripts);
		Check(SUCCEEDED(hr) && scripts.empty(), "Unload");

		hr = co_await dbg.Script(DebuggerX::ScriptAction::Run, bad.c_str(), output);
		printf("  Run (throws): 0x%08X: %ls\n", static_cast<unsigned>(hr), output.c_str());
		Check(FAILED(hr), "a script that throws fails");
	}

	co_await dbg.Stop();
	::DeleteFileW(good.c_str());
	::DeleteFileW(bad.c_str());
	::PostQuitMessage(0);
}
