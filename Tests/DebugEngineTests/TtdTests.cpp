// Time Travel Debugging: replaying a trace of src.exe (spike: what the engine reports)
// The trace: DEBUGENGINETESTS_TTD_TRACE (a .run file); SKIP without it. Only the WinDbg app's engine replays one.
#include "Harness.h"
#include <DbgEng.h>

using namespace DebugEngine;

namespace {
	Task<void> Show(DebuggerX& dbg, PCWSTR command) {
		std::wstring output;
		auto hr = co_await dbg.ExecuteCommandCaptured(command, output);
		printf("  > %ls (0x%08X)\n%ls\n", command, hr, output.c_str());
	}

	Task<void> Status(DebuggerX& dbg, const char* after) {
		ULONG status = 0, cls = 0, qualifier = 0;
		co_await dbg.RunAsync([&](IDebugClient* client) -> HRESULT {
			CComQIPtr<IDebugControl> control(client);
			control->GetExecutionStatus(&status);
			return control->GetDebuggeeType(&cls, &qualifier);
			});
		printf("  [%s] engine status %u, notified %u, class %u, qualifier %u\n", after, status, static_cast<ULONG>(g_Events.Status), cls, qualifier);
	}
}

Task<void> TtdSuite(DebuggerX& dbg) {
	wchar_t trace[MAX_PATH]{};
	if (!::GetEnvironmentVariableW(L"DEBUGENGINETESTS_TTD_TRACE", trace, _countof(trace))) {
		printf("SKIP: no DEBUGENGINETESTS_TTD_TRACE\n");
		::PostQuitMessage(0);
		co_return;
	}

	auto hr = co_await dbg.OpenDumpFile(trace);
	printf("  OpenDumpFile: 0x%08X, target type %d\n", hr, static_cast<int>(dbg.GetTargetType()));
	if (hr == E_INVALIDARG) {
		// (the Windows Kits debugger's and System32's engines cannot replay a trace: only the WinDbg app's can)
		printf("SKIP: this engine does not replay TTD traces\n");
		::PostQuitMessage(0);
		co_return;
	}
	Check(SUCCEEDED(hr) && dbg.GetTargetType() == TargetType::TraceUser, "the trace opens, as a TraceUser target");
	if (FAILED(hr)) {
		::PostQuitMessage(0);
		co_return;
	}
	co_await Status(dbg, "opened");
	co_await Where(dbg);
	co_await Show(dbg, L"dx @$curprocess.TTD.Lifetime");
	co_await Show(dbg, L"dx @$curthread.TTD.Position");
	co_await Show(dbg, L"dx -r1 @$curprocess.TTD.Events");

	// forward to a source line, then back
	hr = co_await dbg.AddBreakpoint(L"`src.cpp:13`");
	printf("  bp src.cpp:13: 0x%08X\n", hr);
	hr = co_await dbg.Go();
	printf("  Go: 0x%08X\n", hr);
	co_await Status(dbg, "after g");
	auto loc = co_await Where(dbg);
	Check(loc.Line == 13, "g forward stops at line 13");
	co_await Show(dbg, L"dx @$curthread.TTD.Position");

	co_await Show(dbg, L"p");
	co_await Status(dbg, "after p");
	co_await Where(dbg);
	co_await Show(dbg, L"p-");
	co_await Status(dbg, "after p-");
	loc = co_await Where(dbg);
	Check(loc.Line == 13, "p- goes back to line 13");

	co_await Show(dbg, L"g-");
	co_await Status(dbg, "after g- (to the start)");
	co_await Where(dbg);
	co_await Show(dbg, L"dx @$curthread.TTD.Position");

	co_await dbg.ExecuteCommand(L"bc *");
	co_await Show(dbg, L"g");
	co_await Status(dbg, "after g (to the end)");
	co_await Where(dbg);
	co_await Show(dbg, L"dx @$curthread.TTD.Position");

	co_await Show(dbg, L"dx -r1 @$cursession.TTD.Calls(\"src!Twice\")");
	co_await Show(dbg, L"!tt 0");
	co_await Status(dbg, "after !tt 0");
	co_await Show(dbg, L"dx @$curthread.TTD.Position");

	co_await dbg.Stop();
	co_await Status(dbg, "stopped");
	::PostQuitMessage(0);
}
