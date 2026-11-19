// Breakpoint conditions, pass counts, one-shot and data breakpoints (loop.exe)
#include "Harness.h"

using namespace DebugEngine;

static Task<ULONG64> Iteration(DebuggerX& dbg) {
	co_return co_await Eval(dbg, L"poi(loop!g_iter) & 0xffffffff");
}

Task<void> BreakpointSuite(DebuggerX& dbg) {
	auto hr = co_await dbg.LaunchAndAttach(Target(L"loop").c_str());
	Check(SUCCEEDED(hr), "launch loop.exe");
	co_await dbg.ExecuteCommand(L"bp loop!main");
	co_await dbg.ExecuteCommand(L"g");
	co_await dbg.ExecuteCommand(L"bc *");

	// a condition on a global
	BreakpointSettings s;
	s.Expression = L"loop!Work";
	s.Condition = L"g_iter == 3";
	hr = co_await dbg.AddBreakpoint(s);
	Check(SUCCEEDED(hr), "add a conditional breakpoint");
	co_await dbg.ExecuteCommand(L"g");
	Check(co_await Iteration(dbg) == 3, "condition g_iter == 3: stops at the 4th call");
	auto bps = co_await Breakpoints(dbg);
	Check(bps.size() == 1 && bps[0].Condition == L"g_iter == 3" && bps[0].Hits == 1, "condition and 1 hit reported");
	const ULONG id = bps.empty() ? 0 : bps[0].Id;

	// a pass count, no condition
	s.Condition.clear();
	s.PassCount = 3;
	hr = co_await dbg.UpdateBreakpoint(id, s);
	Check(SUCCEEDED(hr), "update: no condition, break on the 3rd hit");
	co_await dbg.ExecuteCommand(L"g");
	Check(co_await Iteration(dbg) == 6, "pass count 3: 3 more calls (4, 5, 6)");
	bps = co_await Breakpoints(dbg);
	Check(bps.size() == 1 && bps[0].PassCount == 3 && bps[0].Condition.empty() && bps[0].Hits == 2, "pass count reported, hits 2");
	co_await dbg.RemoveBreakpoint(id);

	// one-shot (i == 7)
	s = {};
	s.Expression = L"loop!Work";
	s.OneShot = true;
	co_await dbg.AddBreakpoint(s);
	co_await dbg.ExecuteCommand(L"g");
	Check(co_await Iteration(dbg) == 7, "one-shot breakpoint hit (i == 7)");
	Check((co_await Breakpoints(dbg)).empty(), "one-shot breakpoint gone after it is hit");

	// a condition on a local, inside the function body (past the prologue): a source line
	s = {};
	s.Expression = L"`loop.cpp:9`";
	s.Condition = L"doubled == 16";
	hr = co_await dbg.AddBreakpoint(s);
	Check(SUCCEEDED(hr), "breakpoint on a source line with a condition on a local");
	co_await dbg.ExecuteCommand(L"g");
	Check(co_await Iteration(dbg) == 8, "condition on a local (doubled == 16) stops at i == 8");
	co_await dbg.ExecuteCommand(L"bc *");

	// data breakpoint: misaligned, then a good one
	s = {};
	s.IsData = true;
	s.DataSize = 4;
	s.DataAccess = DEBUG_BREAK_WRITE;
	s.Expression = L"loop!g_count + 1";
	hr = co_await dbg.AddBreakpoint(s);
	Check(hr == HRESULT_FROM_WIN32(ERROR_INVALID_ADDRESS), "misaligned data breakpoint refused");
	Check((co_await Breakpoints(dbg)).empty(), "and not left behind");

	s.Expression = L"loop!g_count";
	hr = co_await dbg.AddBreakpoint(s);
	Check(SUCCEEDED(hr), "data breakpoint on g_count (write 4)");
	bps = co_await Breakpoints(dbg);
	auto address = co_await Eval(dbg, L"loop!g_count");
	Check(bps.size() == 1 && bps[0].IsData && bps[0].DataSize == 4 && bps[0].DataAccess == DEBUG_BREAK_WRITE && bps[0].Offset == address,
		"data breakpoint reported: size, access, address");
	auto before = co_await Eval(dbg, L"poi(loop!g_count) & 0xffffffff");
	co_await dbg.ExecuteCommand(L"g");
	auto after = co_await Eval(dbg, L"poi(loop!g_count) & 0xffffffff");
	std::vector<StackFrameInfo> frames;
	co_await dbg.GetStackTrace(frames);
	Check(after != before && !frames.empty() && frames[0].Symbol.find(L"Work") != std::wstring::npos,
		"data breakpoint stops in Work right after the write");

	// the type cannot change
	s.IsData = false;
	hr = co_await dbg.UpdateBreakpoint(bps.empty() ? 0 : bps[0].Id, s);
	Check(hr == E_INVALIDARG, "data -> code refused");
	co_await dbg.ExecuteCommand(L"bc *");

	// a condition that does not evaluate: breaks
	s = {};
	s.Expression = L"loop!Work";
	s.Condition = L"no_such_thing_xyz > 1";
	co_await dbg.AddBreakpoint(s);
	auto iteration = co_await Iteration(dbg);
	co_await dbg.ExecuteCommand(L"g");
	Check(co_await Iteration(dbg) == iteration + 1, "a bad condition breaks (at the next call)");
	co_await dbg.ExecuteCommand(L"bc *");

	// a condition that is never true: runs to the end
	s = {};
	s.Expression = L"loop!Work";
	s.Condition = L"g_iter == 100";
	co_await dbg.AddBreakpoint(s);
	co_await dbg.ExecuteCommand(L"g");
	bps = co_await Breakpoints(dbg);
	auto total = co_await Eval(dbg, L"poi(loop!g_count) & 0xffffffff");
	Check(total == 90 && bps.size() == 1 && bps[0].Hits == 0, "a never-true condition never stops: all iterations run");

	co_await dbg.Stop();
	::PostQuitMessage(0);
}
