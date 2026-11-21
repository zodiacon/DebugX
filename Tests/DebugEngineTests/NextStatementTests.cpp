// Set Next Statement; a source line at the initial break; Restart as CMainFrame does it (loop.exe)
#include "Harness.h"

using namespace DebugEngine;

Task<void> NextStatementSuite(DebuggerX& dbg) {
	const auto exe = Target(L"loop");
	auto hr = co_await dbg.LaunchAndAttach(exe.c_str());
	Check(SUCCEEDED(hr), "launch loop.exe");

	// at the initial break of a first session, a file:line is resolved (the System32 engine has loaded no symbols there:
	// LaunchAndAttach loads the executable's)
	hr = co_await dbg.AddBreakpoint(dbg.SourceLineLocation(L"loop.cpp", 9).c_str());
	auto bps = co_await Breakpoints(dbg);
	Check(SUCCEEDED(hr) && bps.size() == 1 && !bps[0].Deferred, "a file:line set at the initial break is not deferred");
	co_await dbg.Go();
	auto loc = co_await Where(dbg);
	Check(loc.Line == 9, "and it hits");
	co_await dbg.ExecuteCommand(L"bc *");

	co_await dbg.RunTo(dbg.SourceLineLocation(L"loop.cpp", 17).c_str());
	loc = co_await Where(dbg);
	Check(loc.Line == 17, "at line 17");

	// back to line 15 (the same function): the IP moves, ContextChanged comes
	ULONG64 target = 0;
	hr = co_await dbg.ResolveLocation(dbg.SourceLineLocation(loc.File, 15).c_str(), target);
	Check(SUCCEEDED(hr) && target, "ResolveLocation of a source line");
	auto contexts = g_Events.ContextChanged;
	hr = co_await dbg.SetInstructionOffset(target);
	Check(SUCCEEDED(hr), "SetInstructionOffset in the same function");
	ULONG64 ip = 0;
	co_await dbg.GetInstructionOffset(ip);
	Check(ip == target, "the IP is the new one");
	loc = co_await Where(dbg);
	Check(loc.Line == 15, "now on line 15");
	// (the notification is posted: it comes before the next call completes)
	co_await dbg.OutputPrompt();
	Check(g_Events.ContextChanged > contexts, "ContextChanged after it");
	std::vector<RegisterInfo> regs;
	co_await dbg.GetRegisters(regs);
	Check(std::any_of(regs.begin(), regs.end(), [=](auto& r) { return r.Name == L"rip" && wcstoull(r.Value.c_str(), nullptr, 16) == target; }),
		"the Registers pane sees the new rip");

	// into another function: refused, unless asked for
	auto work = co_await Eval(dbg, (L"`" + loc.File + L":8`").c_str());
	hr = co_await dbg.SetInstructionOffset(work);
	Check(hr == SetNextOtherFunction, "another function: SetNextOtherFunction");
	co_await dbg.GetInstructionOffset(ip);
	Check(ip == target, "and the IP did not move");
	hr = co_await dbg.SetInstructionOffset(work, true);
	co_await dbg.GetInstructionOffset(ip);
	Check(SUCCEEDED(hr) && ip == work, "forced: it moves");
	co_await dbg.SetInstructionOffset(target, true);

	// running from the moved IP runs line 15 again: Work is called and its breakpoint hits
	co_await dbg.AddBreakpoint((L"`" + loc.File + L":8`").c_str());
	co_await dbg.ExecuteCommand(L"g");
	loc = co_await Where(dbg);
	Check(loc.Line == 8, "running from the moved IP: Work's line 8 hits");

	// Restart, as CMainFrame does it: the breakpoints read, Stop, launch again, the breakpoints set again
	bps = co_await Breakpoints(dbg);
	co_await dbg.Stop();
	hr = co_await dbg.LaunchAndAttach(exe.c_str());
	Check(SUCCEEDED(hr), "relaunched");
	bool added = !bps.empty();
	for (auto& bp : bps)
		added &= SUCCEEDED(co_await dbg.AddBreakpoint(static_cast<BreakpointSettings>(bp)));
	Check(added, "the breakpoints set again");
	co_await dbg.ExecuteCommand(L"g");
	loc = co_await Where(dbg);
	Check(loc.Line == 8, "after the restart the breakpoint on line 8 hits");

	co_await dbg.Stop();
	::PostQuitMessage(0);
}
