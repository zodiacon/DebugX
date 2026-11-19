// Source lines: stepping by line, breakpoints and run-to on a line, symbol and source paths (loop.exe, src.exe)
#include "Harness.h"

using namespace DebugEngine;

Task<void> SourceSuite(DebuggerX& dbg) {
	auto hr = co_await dbg.LaunchAndAttach(Target(L"loop").c_str());
	Check(SUCCEEDED(hr), "launch loop.exe");
	auto level = co_await CodeLevel(dbg);

	// at the initial breakpoint (ntdll, no lines): a source step is an instruction step
	ULONG64 before = 0, after = 0;
	co_await dbg.GetInstructionOffset(before);
	auto loc = co_await Where(dbg);
	Check(loc.File.empty(), "no line information at the initial breakpoint");
	hr = co_await dbg.Step(StepKind::Over, true);
	co_await dbg.GetInstructionOffset(after);
	Check(SUCCEEDED(hr) && after > before && after - before < 16, "a source step without lines steps one instruction");
	Check(co_await CodeLevel(dbg) == level, "the code level is restored");

	co_await dbg.ExecuteCommand(L"bp loop!main");
	co_await dbg.ExecuteCommand(L"g");
	co_await dbg.ExecuteCommand(L"bc *");
	loc = co_await Where(dbg);
	Check(EndsWith(loc.File, L"loop.cpp") && loc.Line == 12, "main is at loop.cpp(12)");
	Check(!loc.Path.empty() && ::GetFileAttributesW(loc.Path.c_str()) != INVALID_FILE_ATTRIBUTES, "loop.cpp found on disk");

	hr = co_await dbg.Step(StepKind::Over, true);
	loc = co_await Where(dbg);
	Check(SUCCEEDED(hr) && loc.Line == 13, "step over by source line: line 13");
	co_await dbg.Step(StepKind::Over, true);
	loc = co_await Where(dbg);
	Check(loc.Line == 14, "line 14");
	co_await dbg.Step(StepKind::Over, true);
	loc = co_await Where(dbg);
	Check(loc.Line == 15, "line 15");
	co_await dbg.Step(StepKind::Into, true);
	loc = co_await Where(dbg);
	Check(loc.Line == 7, "step into Work: line 7 (through the incremental-linking thunk)");
	Check(co_await CodeLevel(dbg) == level, "the code level is restored after source steps");

	// the scope frame, as .frame chooses it: the event, and GetScope (the System32 engine's own index fails)
	g_Events.Scopes.clear();
	co_await dbg.ExecuteCommand(L".frame 1");
	Check(std::find(g_Events.Scopes.begin(), g_Events.Scopes.end(), 1u) != g_Events.Scopes.end(), ".frame 1 says ScopeChanged(1)");
	ULONG64 frameIp = 0;
	ULONG frame = 99;
	co_await dbg.GetScope(frameIp, frame);
	std::vector<SourceLocation> callers;
	co_await dbg.GetSourceLocations({ frameIp - 1 }, callers);
	Check(frame == 1 && !callers.empty() && callers[0].Line == 15, "GetScope: frame 1, called from line 15");
	co_await dbg.ExecuteCommand(L".frame 0");

	// instruction steps stay on the line a while
	co_await dbg.Step(StepKind::Over, false);
	loc = co_await Where(dbg);
	Check(loc.Line == 7, "an instruction step stays on line 7");

	co_await dbg.Step(StepKind::Out, true);
	loc = co_await Where(dbg);
	Check(loc.Line == 15, "step out: back on line 15");

	// a breakpoint on a source line, as the Source view sets it
	hr = co_await dbg.AddBreakpoint((L"`" + loc.File + L":9`").c_str());
	Check(SUCCEEDED(hr), "breakpoint on `loop.cpp:9`");
	auto bps = co_await Breakpoints(dbg);
	std::vector<SourceLocation> bpLocations;
	if (!bps.empty())
		co_await dbg.GetSourceLocations({ bps[0].Offset }, bpLocations);
	Check(!bpLocations.empty() && bpLocations[0].Line == 9, "the breakpoint maps back to line 9");
	co_await dbg.ExecuteCommand(L"g");
	loc = co_await Where(dbg);
	Check(loc.Line == 9, "stopped at line 9");
	co_await dbg.ExecuteCommand(L"bc *");

	// run to a line
	co_await dbg.ExecuteCommand((L"g `" + loc.File + L":17`").c_str());
	loc = co_await Where(dbg);
	Check(loc.Line == 17, "g `loop.cpp:17` stops at line 17");
	// into printf: the CRT's lines are in the PDB, but not its files; and back
	co_await dbg.Step(StepKind::Into, true);
	co_await dbg.Step(StepKind::Out, true);
	loc = co_await Where(dbg);
	Check(loc.Line == 17 || loc.Line == 18, "out of printf: back in main (17, or 18 when the call ends the line)");

	// paths
	std::wstring symbolPath, sourcePath;
	hr = co_await dbg.GetSymbolPaths(symbolPath, sourcePath);
	Check(SUCCEEDED(hr), "GetSymbolPaths");
	co_await dbg.SetSourcePath(L"C:\\nowhere;C:\\Windows");
	co_await dbg.SetSymbolPath(L"C:\\nowhere-symbols");
	std::wstring symbolPath2, sourcePath2;
	co_await dbg.GetSymbolPaths(symbolPath2, sourcePath2);
	Check(symbolPath2 == L"C:\\nowhere-symbols" && sourcePath2 == L"C:\\nowhere;C:\\Windows", "paths set and read back");
	loc = co_await Where(dbg);
	Check(!loc.Path.empty(), "a file where it was compiled is found whatever the source path");
	hr = co_await dbg.ReloadSymbols();
	Check(SUCCEEDED(hr), "ReloadSymbols");
	co_await dbg.SetSymbolPath(symbolPath.c_str());
	co_await dbg.SetSourcePath(sourcePath.c_str());
	co_await dbg.ReloadSymbols();
	loc = co_await Where(dbg);
	Check(loc.Line != 0, "lines back after restoring the symbol path");

	// one module's symbols, now (Modules > Load Symbols)
	auto symbolsOf = [&](PCWSTR name) -> Task<ULONG> {
		std::vector<ModuleInfo> modules;
		co_await dbg.EnumModules(modules);
		auto it = std::find_if(modules.begin(), modules.end(), [=](auto& m) { return _wcsicmp(m.Name.c_str(), name) == 0; });
		co_return it == modules.end() ? 99u : it->Symbols;
	};
	auto kernel32 = co_await symbolsOf(L"kernel32");
	hr = co_await dbg.ReloadSymbols(L"kernel32.dll");
	auto loaded = co_await symbolsOf(L"kernel32");
	printf("  kernel32 symbols: %u -> %u\n", kernel32, loaded);
	Check(SUCCEEDED(hr) && loaded != 99 && loaded != DEBUG_SYMTYPE_DEFERRED, "ReloadSymbols(kernel32.dll): kernel32's symbols are loaded");
	co_await dbg.Stop();

	// stepping into code without lines (an import) steps over it
	hr = co_await dbg.LaunchAndAttach(Target(L"src").c_str());
	Check(SUCCEEDED(hr), "launch src.exe");
	co_await dbg.ExecuteCommand(L"bp src!main");
	co_await dbg.ExecuteCommand(L"g");
	co_await dbg.ExecuteCommand(L"bc *");
	co_await dbg.Step(StepKind::Over, true);
	loc = co_await Where(dbg);
	Check(loc.Line == 12, "src.exe: line 12");
	co_await dbg.Step(StepKind::Into, true);
	loc = co_await Where(dbg);
	Check(loc.Line == 13, "step into GetTickCount steps over it: line 13");
	co_await dbg.Step(StepKind::Into, true);
	loc = co_await Where(dbg);
	Check(loc.Line == 7, "step into a line with an import and a function: into the function (line 7)");

	co_await dbg.Stop();
	::PostQuitMessage(0);
}
