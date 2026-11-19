// Locals, watches and scope frames; writing memory and registers (scope.exe)
#include "Harness.h"

using namespace DebugEngine;

Task<void> ScopeSuite(DebuggerX& dbg) {
	auto hr = co_await dbg.LaunchAndAttach(Target(L"scope").c_str(), L"first second");
	Check(SUCCEEDED(hr), "launch scope.exe");
	// (past wmain's prologue: locals are addressed from a frame register it sets up)
	co_await dbg.ExecuteCommand(L"g `scope.cpp:11`");

	std::vector<StackFrameInfo> frames;
	co_await dbg.GetStackTrace(frames);
	Check(!frames.empty() && frames[0].Symbol.find(L"wmain") != std::wstring::npos, "stopped in wmain");

	std::vector<SymbolEntry> locals;
	hr = co_await dbg.GetLocals(locals, {});
	Dump("locals, frame 0", locals);
	Check(SUCCEEDED(hr) && Has(locals, L"argc") && Has(locals, L"argv") && Has(locals, L"total"), "frame 0 has argc, argv and total");

	// argv expanded by path: its child (the pointed-to pointer) shows up at depth 1
	locals.clear();
	hr = co_await dbg.GetLocals(locals, { L"argv" });
	bool argvExpanded = false;
	for (size_t i = 0; i + 1 < locals.size(); i++)
		if (locals[i].Name == L"argv" && locals[i].Expanded && locals[i + 1].Depth == 1)
			argvExpanded = true;
	Check(argvExpanded, "argv expands by path");

	std::vector<SymbolEntry> watches;
	hr = co_await dbg.EvaluateWatches(watches, { L"argc", L"no_such_thing_xyz", L"argc + 41" }, {});
	Dump("watches", watches);
	Check(std::count_if(watches.begin(), watches.end(), [](auto& e) { return e.Depth == 0; }) == 3, "one root per watch expression");
	Check(watches.size() >= 3 && watches[0].Name == L"argc" && watches[0].Value.find(L'3') != std::wstring::npos && !watches[0].Error,
		"watch argc evaluates (3)");
	Check(std::any_of(watches.begin(), watches.end(), [](auto& e) { return e.Name == L"no_such_thing_xyz" && e.Error; }),
		"an unknown name is an error row");

	// another frame
	g_Events.Scopes.clear();
	hr = co_await dbg.SetScopeFrame(1);
	Check(SUCCEEDED(hr), "SetScopeFrame(1)");
	ULONG64 ip = 0;
	ULONG frame = 99;
	co_await dbg.GetScope(ip, frame);
	Check(frame == 1 && frames.size() > 1 && ip == frames[1].InstructionOffset, "GetScope reports frame 1 and its IP");
	Check(std::find(g_Events.Scopes.begin(), g_Events.Scopes.end(), 1u) != g_Events.Scopes.end(), "ScopeChanged(1)");

	locals.clear();
	co_await dbg.GetLocals(locals, {});
	Check(!Has(locals, L"total"), "frame 1's locals are not wmain's");

	std::vector<RegisterInfo> regs;
	hr = co_await dbg.GetRegisters(regs);
	Check(SUCCEEDED(hr) && !regs.empty(), "frame registers read");

	// a thread walk (as the Threads pane does) must not lose the chosen frame
	std::vector<ProcessInfo> processes;
	std::vector<ThreadInfo> threads;
	co_await dbg.EnumProcesses(processes, threads);
	frame = 99;
	co_await dbg.GetScope(ip, frame);
	Check(frame == 1, "the frame survives EnumProcesses");

	// ---- writing: memory
	co_await dbg.SetScopeFrame(0);
	auto str = co_await Eval(dbg, L"poi(argv)");
	Check(str != ~0ull && str != 0, "argv[0] address");

	g_Events.MemoryChanged = 0;
	hr = co_await dbg.WriteMemory(str, { 'Z', 0, 'Y', 0 });
	Check(SUCCEEDED(hr), "WriteMemory succeeds");
	Check(g_Events.MemoryChanged == 1, "exactly one MemoryChanged, delivered before the task completed");
	std::vector<BYTE> back;
	co_await dbg.ReadMemory(str, back, 4);
	Check(back.size() == 4 && back[0] == 'Z' && back[2] == 'Y', "written bytes read back");

	g_Events.MemoryChanged = 0;
	co_await dbg.ExecuteCommand(std::format(L"eb 0x{:X} 0x58", str).c_str());
	back.clear();
	co_await dbg.ReadMemory(str, back, 1);
	Check(back.size() == 1 && back[0] == 'X', "eb wrote");
	Check(g_Events.MemoryChanged >= 1, "eb raises MemoryChanged");

	hr = co_await dbg.WriteMemory(0x10, { 1, 2, 3 });
	Check(FAILED(hr), "a write to an unmapped address fails");

	g_Events.MemoryChanged = 0;
	co_await dbg.ExecuteCommand(L"p");
	Check(g_Events.MemoryChanged == 0, "a step raises no MemoryChanged");

	// ---- writing: registers
	g_Events.ContextChanged = 0;
	hr = co_await dbg.SetRegister(L"rax", L"1234");
	Check(SUCCEEDED(hr), "SetRegister rax");
	Check(g_Events.ContextChanged >= 1, "ContextChanged after SetRegister");
	Check(co_await Eval(dbg, L"@rax") == 0x1234, "rax reads back 0x1234 (hexadecimal, as shown)");
	regs.clear();
	co_await dbg.GetRegisters(regs);
	Check(std::any_of(regs.begin(), regs.end(), [](auto& r) { return r.Name == L"rax" && r.Value == L"0000000000001234"; }),
		"GetRegisters (what the pane shows) has the new rax");

	hr = co_await dbg.SetRegister(L"rax", L"@rax + 1");
	Check(SUCCEEDED(hr) && co_await Eval(dbg, L"@rax") == 0x1235, "an expression sets rax");

	hr = co_await dbg.SetRegister(L"rax", L"no_such_thing_xyz");
	Check(FAILED(hr), "a bad expression fails");
	hr = co_await dbg.SetRegister(L"xmm0", L"1");
	Check(hr == E_NOTIMPL, "a vector register is E_NOTIMPL");
	hr = co_await dbg.SetRegister(L"st0", L"2.5");
	regs.clear();
	co_await dbg.GetRegisters(regs);
	Check(SUCCEEDED(hr) && std::any_of(regs.begin(), regs.end(), [](auto& r) { return r.Name == L"st0" && r.Value == L"2.5"; }),
		"a floating-point register takes a number");

	co_await dbg.Stop();
	::PostQuitMessage(0);
}
