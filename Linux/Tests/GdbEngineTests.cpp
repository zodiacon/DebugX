//
// GdbEngineTests: GdbDebugger against small programs it debugs (Targets/*.cpp), as Tests/DebugEngineTests tests DebuggerX
// on Windows. Each suite is a coroutine on an EventLoop with a debugger of its own, as the GUI uses it. With no arguments
// every suite runs; suite names pick some. The exit code is the number of failed checks. DEBUGENGINETESTS_OUTPUT=1 prints
// the engine's output.
//
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <signal.h>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include "EventLoop.h"
#include "GdbDebugger.h"
#include "Utf8.h"

using namespace DebugEngine;
using namespace GdbEngine;
using namespace std::chrono_literals;

extern char** environ;

namespace {
	int g_Failed, g_Passed;
	bool g_Verbose;

	void Check(bool ok, const char* what, const char* file, int line) {
		if (ok)
			g_Passed++;
		else {
			g_Failed++;
			std::printf("  FAILED: %s (%s:%d)\n", what, std::filesystem::path(file).filename().c_str(), line);
		}
	}
#define CHECK(x) Check(static_cast<bool>(x), #x, __FILE__, __LINE__)
#define CHECK_HR(x) do { auto hr_ = (x); Check(SUCCEEDED(hr_), #x, __FILE__, __LINE__); if (FAILED(hr_)) std::printf("    hr=0x%08X %ls\n", static_cast<unsigned>(hr_), d.LastError().c_str()); } while (0)

	std::wstring Target(const char* name) {
		return FromUtf8(std::string(TARGETS_DIR) + "/" + name);
	}

	std::wstring Source(const char* name) {
		return FromUtf8(std::string(TARGET_SOURCES_DIR) + "/" + name);
	}

	// the line of a target's source with the marker (// @name)
	ULONG Line(const char* file, const char* marker) {
		std::ifstream in(std::string(TARGET_SOURCES_DIR) + "/" + file);
		std::string line, tag = std::string("@") + marker;
		for (ULONG n = 1; std::getline(in, line); n++) {
			auto pos = line.find(tag);
			if (pos != std::string::npos && (pos + tag.size() == line.size() || !std::isalnum(static_cast<unsigned char>(line[pos + tag.size()])) && line[pos + tag.size()] != '-'))
				return n;
		}
		std::printf("  no marker %s in %s\n", marker, file);
		return 0;
	}

	// what the debugger told the suite
	struct Events {
		ExecutionStatus Status{ ExecutionStatus::NoDebuggee };
		std::wstring Output, Debuggee;
		std::vector<ExceptionData> Exceptions;
		std::vector<ULONG> Breakpoints;
		std::vector<ULONG> Contexts;
		std::vector<ULONG> Scopes;
		int ExitCode{ -1 };
		int MemoryChanged{ 0 }, BreakpointsChanged{ 0 }, TargetChanged{ 0 }, Busy{ 0 };
		std::set<std::wstring> Modules;

		void Handle(DebuggerNotificationType type, void* data) {
			switch (type) {
				case DebuggerNotificationType::StatusChanged: Status = *static_cast<ExecutionStatus*>(data); break;
				case DebuggerNotificationType::Output: {
					auto& o = *static_cast<OutputData*>(data);
					if ((o.Mask & OutputDataMask::Debuggee) == OutputDataMask::Debuggee)
						Debuggee += o.Text;
					else
						Output += o.Text;
					if (g_Verbose)
						std::printf("%ls", o.Text.c_str());
					break;
				}
				case DebuggerNotificationType::Exception: Exceptions.push_back(*static_cast<ExceptionData*>(data)); break;
				case DebuggerNotificationType::Breakpoint: Breakpoints.push_back(static_cast<BreakpointData*>(data)->Id); break;
				case DebuggerNotificationType::ContextChanged: Contexts.push_back(static_cast<ContextData*>(data)->ThreadId); break;
				case DebuggerNotificationType::ScopeChanged: Scopes.push_back(static_cast<ScopeData*>(data)->FrameIndex); break;
				case DebuggerNotificationType::ExitProcess: ExitCode = static_cast<int>(static_cast<ExitThreadData*>(data)->ExitCode); break;
				case DebuggerNotificationType::MemoryChanged: MemoryChanged++; break;
				case DebuggerNotificationType::BreakpointsChanged: BreakpointsChanged++; break;
				case DebuggerNotificationType::TargetChanged: TargetChanged++; break;
				case DebuggerNotificationType::BusyChanged: Busy++; break;
				case DebuggerNotificationType::ModuleLoaded: Modules.insert(static_cast<ModuleEventData*>(data)->Name); break;
				default: break;
			}
		}
	};

	EventLoop* g_Loop;

	// resumes the coroutine on the loop after a while
	struct Delay {
		std::chrono::milliseconds Time;
		bool await_ready() const noexcept { return false; }
		void await_suspend(std::coroutine_handle<> h) {
			std::thread([h, t = Time] {
				std::this_thread::sleep_for(t);
				g_Loop->Post([h] { h.resume(); });
			}).detach();
		}
		void await_resume() noexcept {}
	};

	Task<ULONG64> Pc(GdbDebugger& d) {
		ULONG64 pc = 0;
		co_await d.GetInstructionOffset(pc);
		co_return pc;
	}

	Task<ULONG> CurrentLine(GdbDebugger& d) {
		auto pc = co_await Pc(d);
		std::vector<SourceLocation> locations;
		co_await d.GetSourceLocations({ pc }, locations);
		co_return locations.empty() ? 0 : locations[0].Line;
	}

	Task<ULONG64> Eval(GdbDebugger& d, const wchar_t* expr) {
		ULONG64 v = ~0ull;
		co_await d.Evaluate(expr, v);
		co_return v;
	}

	const SymbolEntry* FindEntry(const std::vector<SymbolEntry>& entries, const std::wstring& path) {
		for (auto& e : entries)
			if (e.Path == path)
				return &e;
		return nullptr;
	}

	//
	// Suites
	//

	Task<void> LaunchSuite(GdbDebugger& d, Events& e) {
		CHECK_HR(co_await d.LaunchAndAttach(Target("loop").c_str()));
		CHECK(e.Status == ExecutionStatus::Break);
		CHECK(d.GetTargetType() == TargetType::LiveUser);
		CHECK(e.Contexts.size() >= 1);

		std::vector<StackFrameInfo> frames;
		CHECK_HR(co_await d.GetStackTrace(frames));
		CHECK(!frames.empty() && frames[0].Symbol.find(L"main") != std::wstring::npos);
		CHECK(!frames.empty() && frames[0].StackOffset != 0);

		std::vector<SourceLocation> locations;
		CHECK_HR(co_await d.GetSourceLocations({ frames.empty() ? 0 : frames[0].InstructionOffset, 0 }, locations));
		CHECK(locations.size() == 2);
		if (locations.size() == 2) {
			CHECK(locations[0].File.ends_with(L"loop.cpp"));
			CHECK(locations[0].Line > Line("loop.cpp", "work") && locations[0].Line < Line("loop.cpp", "call-work"));
			CHECK(locations[0].Path == Source("loop.cpp"));
			CHECK(locations[1].Line == 0 && locations[1].File.empty());
		}

		std::vector<ModuleInfo> modules;
		CHECK_HR(co_await d.EnumModules(modules));
		bool hasExe = false, hasLibc = false;
		for (auto& m : modules) {
			hasExe |= m.Name == L"loop" && m.BaseAddress != 0;
			hasLibc |= m.Name.starts_with(L"libc.so");
		}
		CHECK(hasExe);
		CHECK(hasLibc);

		std::vector<ProcessInfo> processes;
		std::vector<ThreadInfo> threads;
		CHECK_HR(co_await d.EnumProcesses(processes, threads));
		CHECK(processes.size() == 1 && processes[0].ProcessId != 0 && processes[0].Current);
		CHECK(threads.size() == 1 && threads[0].Current && threads[0].ThreadId == (processes.empty() ? 0 : processes[0].ProcessId));

		std::vector<RegisterInfo> registers;
		CHECK_HR(co_await d.GetRegisters(registers));
		bool hasRip = false;
		for (auto& r : registers)
			hasRip |= r.Name == L"rip" && r.Value.starts_with(L"0x");
		CHECK(hasRip);

		auto pc = co_await Pc(d);
		std::vector<DisassemblyLine> lines;
		CHECK_HR(co_await d.Disassemble(pc, 5, lines));
		CHECK(lines.size() == 5 && lines[0].Address == pc && lines[1].Address > pc);

		ULONG64 sp = 0;
		CHECK_HR(co_await d.Evaluate(d.StackPointerExpression().c_str(), sp));
		CHECK(sp != 0);

		std::vector<BYTE> bytes;
		CHECK_HR(co_await d.ReadMemory(pc, bytes, 16));
		CHECK(bytes.size() == 16);
		CHECK(FAILED(co_await d.ReadMemory(0, bytes, 16)) && bytes.empty());

		CHECK(!d.IsBusy());
		CHECK_HR(co_await d.Stop());
		CHECK(e.Status == ExecutionStatus::NoDebuggee);
		CHECK(d.GetTargetType() == TargetType::Unknown);
	}

	Task<void> BreakpointSuite(GdbDebugger& d, Events& e) {
		CHECK_HR(co_await d.LaunchAndAttach(Target("loop").c_str()));
		auto workLine = Line("loop.cpp", "work");
		auto location = d.SourceLineLocation(Source("loop.cpp"), workLine);
		CHECK_HR(co_await d.AddBreakpoint(location.c_str()));
		CHECK(e.BreakpointsChanged > 0);

		std::vector<BreakpointInfo> bps;
		CHECK_HR(co_await d.EnumBreakpoints(bps));
		CHECK(bps.size() == 1);
		if (bps.size() != 1)
			co_return;
		auto id = bps[0].Id;
		CHECK(!bps[0].Deferred && bps[0].Offset != 0 && bps[0].Enabled && !bps[0].Location.empty());

		CHECK_HR(co_await d.Go());
		CHECK(e.Status == ExecutionStatus::Break);
		CHECK(!e.Breakpoints.empty() && e.Breakpoints.back() == id);
		CHECK(co_await CurrentLine(d) == workLine);
		CHECK(co_await Eval(d, L"i") == 0);

		auto settings = static_cast<BreakpointSettings>(bps[0]);
		settings.Condition = L"i == 5";
		CHECK_HR(co_await d.UpdateBreakpoint(id, settings));
		CHECK_HR(co_await d.Go());
		CHECK(co_await Eval(d, L"i") == 5);

		settings.Condition.clear();
		settings.PassCount = 3;
		CHECK_HR(co_await d.UpdateBreakpoint(id, settings));
		CHECK_HR(co_await d.EnumBreakpoints(bps));
		CHECK(bps.size() == 1 && bps[0].Condition.empty() && bps[0].PassCount == 3);
		CHECK_HR(co_await d.Go());
		CHECK(co_await Eval(d, L"i") == 8);

		CHECK_HR(co_await d.SetBreakpointEnabled(id, false));
		CHECK_HR(co_await d.EnumBreakpoints(bps));
		CHECK(bps.size() == 1 && !bps[0].Enabled);
		CHECK_HR(co_await d.RemoveBreakpoint(id));
		CHECK_HR(co_await d.EnumBreakpoints(bps));
		CHECK(bps.empty());

		// a data breakpoint on g_count, written in this iteration still
		BreakpointSettings data;
		data.IsData = true;
		data.Expression = L"&g_count";
		data.DataSize = 4;
		data.DataAccess = BreakOnWrite;
		CHECK_HR(co_await d.AddBreakpoint(data));
		CHECK_HR(co_await d.EnumBreakpoints(bps));
		CHECK(bps.size() == 1 && bps[0].IsData);
		auto hits = e.Breakpoints.size();
		CHECK_HR(co_await d.Go());
		CHECK(e.Status == ExecutionStatus::Break && e.Breakpoints.size() == hits + 1);
		CHECK(co_await Eval(d, L"g_count") == 2 * (0 + 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8));
		data.DataSize = 3;
		CHECK(co_await d.AddBreakpoint(data) == E_INVALIDARG);
		for (auto& b : bps)
			CHECK_HR(co_await d.RemoveBreakpoint(b.Id));

		// a breakpoint in a module that is not loaded yet, then gone again: deferred
		CHECK_HR(co_await d.AddBreakpoint(L"nosuchfunction_xyz"));
		CHECK_HR(co_await d.EnumBreakpoints(bps));
		CHECK(bps.size() == 1 && bps[0].Deferred);
		if (!bps.empty())
			CHECK_HR(co_await d.RemoveBreakpoint(bps[0].Id));

		CHECK_HR(co_await d.Go());
		CHECK(e.Status == ExecutionStatus::NoDebuggee);
		CHECK(e.ExitCode == 0);
		CHECK(e.Debuggee.find(L"count=90") != std::wstring::npos);
	}

	Task<void> StepSuite(GdbDebugger& d, Events& e) {
		CHECK_HR(co_await d.LaunchAndAttach(Target("loop").c_str()));
		auto callTwice = Line("loop.cpp", "call-twice");
		CHECK_HR(co_await d.RunTo(d.SourceLineLocation(L"loop.cpp", callTwice).c_str()));
		CHECK(co_await CurrentLine(d) == callTwice);
		std::vector<BreakpointInfo> bps;
		co_await d.EnumBreakpoints(bps);
		CHECK(bps.empty());		// (RunTo's breakpoint is DebugX's own)

		CHECK_HR(co_await d.Step(StepKind::Into, true));
		CHECK(co_await CurrentLine(d) == Line("loop.cpp", "twice"));
		CHECK_HR(co_await d.Step(StepKind::Out, true));
		std::vector<StackFrameInfo> frames;
		co_await d.GetStackTrace(frames);
		CHECK(!frames.empty() && frames[0].Symbol.find(L"Work") != std::wstring::npos);
		CHECK_HR(co_await d.Step(StepKind::Over, true));
		CHECK(co_await CurrentLine(d) == Line("loop.cpp", "work"));
		auto pc = co_await Pc(d);
		CHECK_HR(co_await d.Step(StepKind::Over, false));
		CHECK(co_await Pc(d) != pc);

		CHECK_HR(co_await d.SetScopeFrame(1));
		ULONG64 ip;
		ULONG frame = 0;
		CHECK_HR(co_await d.GetScope(ip, frame));
		CHECK(frame == 1 && !e.Scopes.empty() && e.Scopes.back() == 1);
		std::vector<SymbolEntry> locals;
		CHECK_HR(co_await d.GetLocals(locals, {}));
		CHECK(FindEntry(locals, L"i") != nullptr);
		CHECK_HR(co_await d.SetScopeFrame(0));

		ULONG64 address = 0;
		CHECK_HR(co_await d.ResolveLocation(d.SourceLineLocation(L"loop.cpp", Line("loop.cpp", "call-work")).c_str(), address));
		CHECK(address != 0);
		CHECK_HR(co_await d.RunTo(d.AddressLocation(address).c_str()));
		CHECK(co_await Pc(d) == address);

		// Set Next Statement: back to the call of Work, in main; Work's code is another function
		CHECK(co_await d.SetInstructionOffset(frames.empty() ? 0 : frames[0].InstructionOffset) == SetNextOtherFunction);
		CHECK_HR(co_await d.SetInstructionOffset(address));
		CHECK(co_await Pc(d) == address);

		e.Output.clear();
		CHECK_HR(co_await d.ExecuteCommand(L"print g_iter + 100"));
		CHECK(e.Output.find(L"= 101") != std::wstring::npos);
		CHECK(FAILED(co_await d.ExecuteCommand(L"nosuchcommand")));
		CHECK_HR(co_await d.ExecuteCommand(L"next"));		// (a run typed in: waits for the stop)
		CHECK(e.Status == ExecutionStatus::Break);
		CHECK_HR(co_await d.Stop());
	}

	Task<void> ValuesSuite(GdbDebugger& d, Events& e) {
		CHECK_HR(co_await d.LaunchAndAttach(Target("values").c_str()));
		CHECK_HR(co_await d.RunTo(d.SourceLineLocation(L"values.cpp", Line("values.cpp", "use")).c_str()));

		std::vector<SymbolEntry> entries;
		CHECK_HR(co_await d.GetLocals(entries, {}));
		for (auto name : { L"argc", L"argv", L"point", L"numbers", L"text", L"shape", L"array", L"sum" })
			CHECK(FindEntry(entries, name) != nullptr);
		auto point = FindEntry(entries, L"point");
		CHECK(point && point->Expandable && !point->Expanded && point->Value == L"{X=3 Y=4}" && point->Type == L"Point");

		CHECK_HR(co_await d.GetLocals(entries, { L"point", L"numbers", L"shape", L"shape\nOrigin", L"array" }));
		auto x = FindEntry(entries, L"point\nX");
		CHECK(x && x->Value == L"3" && x->Expression == L"point.X" && x->Depth == 1);
		auto n1 = FindEntry(entries, L"numbers\n[1]");
		CHECK(n1 && n1->Value == L"20");
		auto y = FindEntry(entries, L"shape\nOrigin\nY");
		CHECK(y && y->Value == L"2" && y->Depth == 2 && y->Expression == L"shape.Origin.Y");
		auto a2 = FindEntry(entries, L"array\n2");
		CHECK(a2 && a2->Value == L"9");
		auto array = FindEntry(entries, L"array");
		CHECK(array && array->Value == L"{7, 8, 9}");
		auto text = FindEntry(entries, L"text");
		CHECK(text && text->Value.find(L"hello") != std::wstring::npos);

		CHECK_HR(co_await d.EvaluateWatches(entries, { L"point.X + 1", L"nosuchthing", L"shape.Next->Name" }, {}));
		CHECK(entries.size() == 3);
		if (entries.size() == 3) {
			CHECK(entries[0].Value == L"4" && !entries[0].Error);
			CHECK(entries[1].Error && !entries[1].Value.empty());
			CHECK(entries[2].Value.find(L"second") != std::wstring::npos);
		}

		std::wstring error;
		auto memoryChanged = e.MemoryChanged;
		CHECK_HR(co_await d.AssignValue(L"point.X", L"42", error));
		CHECK(co_await Eval(d, L"point.X") == 42);
		CHECK(e.MemoryChanged > memoryChanged);
		CHECK(FAILED(co_await d.AssignValue(L"point.Nope", L"1", error)) && !error.empty());

		auto buffer = co_await Eval(d, L"&g_buffer");
		std::vector<BYTE> bytes;
		CHECK_HR(co_await d.ReadMemory(buffer, bytes, 16));
		CHECK(bytes.size() == 16 && bytes[0] == 1 && bytes[4] == 2 && bytes[12] == 4);
		CHECK_HR(co_await d.WriteMemory(buffer, { 9, 0, 0, 0 }));
		CHECK(co_await Eval(d, L"g_buffer[0]") == 9);
		ULONG64 found = 0;
		CHECK_HR(co_await d.SearchMemory(buffer, { 3, 0, 0, 0, 4 }, found));
		CHECK(found == buffer + 8);

		CHECK_HR(co_await d.SetRegister(L"rax", L"1234"));
		CHECK(co_await Eval(d, L"$rax") == 0x1234);

		CHECK_HR(d.SetDisplayRadix(16));
		CHECK_HR(co_await d.EvaluateWatches(entries, { L"array[1]" }, {}));
		CHECK(entries.size() == 1 && entries[0].Value == L"0x8");
		CHECK_HR(d.SetDisplayRadix(10));

		// a core file of it, then the core file
		auto core = std::filesystem::temp_directory_path() / std::format("debugx-values-{}.core", ::getpid());
		CHECK_HR(co_await d.WriteDump(core.wstring().c_str(), true));
		CHECK(std::filesystem::exists(core));
		CHECK_HR(co_await d.Stop());
		CHECK(e.Status == ExecutionStatus::NoDebuggee);

		CHECK_HR(co_await d.OpenDumpFile(core.wstring().c_str()));
		CHECK(d.GetTargetType() == TargetType::DumpUser);
		CHECK(e.Status == ExecutionStatus::Break);
		std::vector<StackFrameInfo> frames;
		CHECK_HR(co_await d.GetStackTrace(frames));
		CHECK(!frames.empty() && frames[0].Symbol.find(L"main") != std::wstring::npos);
		CHECK_HR(co_await d.GetLocals(entries, {}));
		point = FindEntry(entries, L"point");
		CHECK(point && point->Value == L"{X=42 Y=4}");
		CHECK(co_await d.Go() == E_UNEXPECTED);
		CHECK_HR(co_await d.Stop());
		CHECK(e.Status == ExecutionStatus::NoDebuggee && d.GetTargetType() == TargetType::Unknown);
		std::filesystem::remove(core);
	}

	Task<void> CrashSuite(GdbDebugger& d, Events& e) {
		std::vector<EventFilter> filters;
		CHECK_HR(co_await d.GetEventFilters(filters, true));
		bool segv = false;
		for (auto& f : filters)
			segv |= f.Code == SIGSEGV && f.ExecutionOption == FilterBreak && f.IsException;
		CHECK(segv);

		CHECK_HR(co_await d.LaunchAndAttach(Target("crash").c_str()));
		CHECK_HR(co_await d.Go());
		CHECK(e.Status == ExecutionStatus::Break);
		CHECK(e.Exceptions.size() == 1 && e.Exceptions[0].Code == SIGSEGV && e.Exceptions[0].Address != 0);
		std::vector<StackFrameInfo> frames;
		CHECK_HR(co_await d.GetStackTrace(frames));
		CHECK(!frames.empty() && frames[0].Symbol.find(L"Crash") != std::wstring::npos);
		CHECK(co_await CurrentLine(d) == Line("crash.cpp", "crash"));
		CHECK(e.Debuggee.find(L"about to crash") != std::wstring::npos);

		// SIGSEGV made to only say so: the change holds
		for (auto& f : filters)
			if (f.Code == SIGUSR1)
				f.ExecutionOption = FilterIgnore;
		CHECK_HR(co_await d.SetEventFilters(filters));
		std::vector<EventFilter> now;
		CHECK_HR(co_await d.GetEventFilters(now));
		bool usr1 = false;
		for (auto& f : now)
			usr1 |= f.Code == SIGUSR1 && f.ExecutionOption == FilterIgnore;
		CHECK(usr1);
		CHECK_HR(co_await d.Stop());
		CHECK(e.Status == ExecutionStatus::NoDebuggee);
	}

	Task<void> ThreadsSuite(GdbDebugger& d, Events& e) {
		CHECK_HR(co_await d.LaunchAndAttach(Target("threads").c_str()));
		CHECK_HR(co_await d.RunTo(d.SourceLineLocation(L"threads.cpp", Line("threads.cpp", "all-ready")).c_str()));
		std::vector<ProcessInfo> processes;
		std::vector<ThreadInfo> threads;
		CHECK_HR(co_await d.EnumProcesses(processes, threads));
		CHECK(threads.size() == 3);
		ULONG worker = 0;
		int current = 0;
		for (auto& t : threads) {
			if (t.Name == L"worker-a")
				worker = t.Id;
			current += t.Current;
			CHECK(t.ThreadId != 0);
		}
		CHECK(worker != 0 && current == 1);
		CHECK_HR(co_await d.SetCurrentThread(worker));
		CHECK(!e.Contexts.empty() && e.Contexts.back() == worker);
		std::vector<StackFrameInfo> frames;
		CHECK_HR(co_await d.GetStackTrace(frames));
		bool inWorker = false;
		for (auto& f : frames)
			inWorker |= f.Symbol.find(L"Worker") != std::wstring::npos;
		CHECK(inWorker);

		std::vector<HandleInfo> handles;
		CHECK_HR(co_await d.EnumHandles(handles));
		CHECK(handles.size() >= 3 && handles[0].Handle == 0);
		CHECK_HR(co_await d.Stop());
	}

	Task<void> BreakSuite(GdbDebugger& d, Events& e) {
		CHECK_HR(co_await d.LaunchAndAttach(Target("threads").c_str(), L"forever"));
		auto go = d.Go();
		co_await Delay{ 300ms };
		CHECK(e.Status == ExecutionStatus::Go);
		CHECK(d.IsBusy());
		CHECK_HR(d.Break());
		CHECK_HR(co_await go);
		CHECK(e.Status == ExecutionStatus::Break);
		CHECK_HR(co_await d.Detach());
		CHECK(e.Status == ExecutionStatus::NoDebuggee);

		// attach to a program that runs already (Yama's ptrace_scope 1 may forbid it: then SKIP)
		pid_t pid;
		auto exe = ToUtf8(Target("threads"));
		char* argv[] = { exe.data(), const_cast<char*>("forever"), nullptr };
		if (::posix_spawn(&pid, exe.c_str(), nullptr, nullptr, argv, environ) != 0) {
			CHECK(!"posix_spawn");
			co_return;
		}
		co_await Delay{ 200ms };
		auto hr = co_await d.AttachToProcess(pid);
		if (hr == E_ACCESSDENIED)
			std::printf("  SKIP attach: %ls\n", d.LastError().c_str());
		else {
			CHECK_HR(hr);
			CHECK(e.Status == ExecutionStatus::Break && d.GetTargetType() == TargetType::LiveUser);
			std::vector<ProcessInfo> processes;
			std::vector<ThreadInfo> threads;
			CHECK_HR(co_await d.EnumProcesses(processes, threads));
			CHECK(processes.size() == 1 && processes[0].ProcessId == static_cast<ULONG>(pid));
			CHECK_HR(co_await d.Detach());
			CHECK(e.Status == ExecutionStatus::NoDebuggee);
		}
		::kill(pid, SIGKILL);
		int status;
		::waitpid(pid, &status, 0);
	}

	struct Suite {
		const char* Name;
		Task<void>(*Run)(GdbDebugger&, Events&);
	};

	Task<void> RunSuite(const Suite& suite, GdbDebugger& d, Events& e, bool& done) {
		try {
			co_await suite.Run(d, e);
		}
		catch (const std::exception& ex) {
			Check(false, ex.what(), __FILE__, __LINE__);
		}
		done = true;
	}
}

int main(int argc, char** argv) {
	g_Verbose = ::getenv("DEBUGENGINETESTS_OUTPUT") != nullptr;
	const Suite suites[] = {
		{ "launch", LaunchSuite },
		{ "bp", BreakpointSuite },
		{ "step", StepSuite },
		{ "values", ValuesSuite },
		{ "crash", CrashSuite },
		{ "threads", ThreadsSuite },
		{ "break", BreakSuite },
	};
	std::set<std::string> chosen(argv + 1, argv + argc);
	for (auto& suite : suites) {
		if (!chosen.empty() && !chosen.contains(suite.Name))
			continue;
		std::printf("%s\n", suite.Name);
		auto failed = g_Failed;
		EventLoop loop;
		g_Loop = &loop;
		Events events;
		{
			GdbDebugger d;
			auto hr = d.Create([&](DebuggerNotificationType type, void* data) { events.Handle(type, data); }, loop.CreateDispatcher());
			if (FAILED(hr)) {
				std::printf("  FAILED: cannot start GDB\n");
				g_Failed++;
				continue;
			}
			bool done = false;
			auto task = RunSuite(suite, d, events, done);
			if (!loop.RunUntil([&] { return done; }, 120s)) {
				std::printf("  FAILED: timed out\n");
				g_Failed++;
			}
		}
		std::printf("  %s\n", g_Failed == failed ? "ok" : "FAILED");
	}
	std::printf("%d passed, %d failed\n", g_Passed, g_Failed);
	return g_Failed;
}
