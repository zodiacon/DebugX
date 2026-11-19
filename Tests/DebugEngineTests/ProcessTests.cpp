// Threads (names, where they are, frozen, suspended) and child processes (procs.exe)
#include "Harness.h"

using namespace DebugEngine;

namespace {
	struct Snapshot {
		std::vector<ProcessInfo> Processes;
		std::vector<ThreadInfo> Threads;

		const ThreadInfo* ThreadNamed(const wchar_t* name) const {
			auto it = std::find_if(Threads.begin(), Threads.end(), [&](auto& t) { return t.Name == name; });
			return it == Threads.end() ? nullptr : &*it;
		}
	};

	Task<Snapshot> Take(DebuggerX& dbg) {
		Snapshot s;
		co_await dbg.EnumProcesses(s.Processes, s.Threads);
		printf("  %zu process(es), %zu thread(s)\n", s.Processes.size(), s.Threads.size());
		for (auto& t : s.Threads)
			printf("    %c%u %u.%u \"%ls\" at %ls suspend %u%s\n", t.Current ? '.' : ' ', t.Id, t.Pid, t.ThreadId, t.Name.c_str(),
				t.Location.c_str(), t.SuspendCount, t.Frozen ? " frozen" : "");
		co_return s;
	}
}

Task<void> ProcessSuite(DebuggerX& dbg) {
	co_await dbg.SetDebugChildProcesses(true);
	auto hr = co_await dbg.LaunchAndAttach(Target(L"procs").c_str());
	Check(SUCCEEDED(hr), "launch procs.exe, its children debugged too");
	co_await dbg.ExecuteCommand(L"g");

	// at the DebugBreak: the main thread and the worker
	auto s = co_await Take(dbg);
	Check(s.Processes.size() == 1 && s.Processes[0].Current && s.Processes[0].ThreadCount == s.Threads.size(), "one process, current, its thread count");
	auto main = s.ThreadNamed(L"Main Thread");
	auto worker = s.ThreadNamed(L"Worker");
	Check(main && worker, "the threads' names (SetThreadDescription)");
	Check(main && main->Current && !main->Location.empty(), "the main thread is the current one, and where it is");
	Check(worker && worker->Location.find(L'!') != std::wstring::npos && worker->InstructionOffset != 0, "where the worker is: module!function");
	Check(worker && !worker->Frozen, "not frozen");

	if (worker) {
		const auto id = worker->Id;
		const auto suspended = worker->SuspendCount;
		hr = co_await dbg.SetThreadFrozen(id, true);
		s = co_await Take(dbg);
		Check(SUCCEEDED(hr) && s.ThreadNamed(L"Worker") && s.ThreadNamed(L"Worker")->Frozen, "freeze the worker");
		hr = co_await dbg.SetThreadFrozen(id, false);
		s = co_await Take(dbg);
		Check(SUCCEEDED(hr) && s.ThreadNamed(L"Worker") && !s.ThreadNamed(L"Worker")->Frozen, "thaw it");

		hr = co_await dbg.SuspendThread(id, true);
		s = co_await Take(dbg);
		Check(SUCCEEDED(hr) && s.ThreadNamed(L"Worker") && s.ThreadNamed(L"Worker")->SuspendCount == suspended + 1, "suspend it: one more");
		hr = co_await dbg.SuspendThread(id, false);
		s = co_await Take(dbg);
		Check(SUCCEEDED(hr) && s.ThreadNamed(L"Worker") && s.ThreadNamed(L"Worker")->SuspendCount == suspended, "resume it");
	}
	hr = co_await dbg.SetThreadFrozen(999, true);
	Check(FAILED(hr), "a thread there is not: fails");

	// handles (the worker waits in a read of a pipe: asking the pipe's name would wait too)
	std::vector<HandleInfo> handles;
	auto start = ::GetTickCount64();
	hr = co_await dbg.EnumHandles(handles);
	printf("  %zu handles in %llu ms\n", handles.size(), ::GetTickCount64() - start);
	for (auto& h : handles)
		if (!h.Name.empty() || h.Type == L"File")
			printf("    0x%llX %ls %ls (0x%X)\n", h.Handle, h.Type.c_str(), h.Name.c_str(), h.GrantedAccess);
	Check(SUCCEEDED(hr) && !handles.empty(), "EnumHandles");
	Check(std::any_of(handles.begin(), handles.end(), [](auto& h) { return h.Type == L"Event" && h.Name.ends_with(L"\\DebugXTestEvent"); }),
		"the named event, by its name");
	Check(std::any_of(handles.begin(), handles.end(), [](auto& h) { return h.Type == L"Thread"; }), "thread handles");

	// searching memory: the event's name is in the program's constants
	std::wstring needle = L"DebugXTestEvent";
	std::vector<BYTE> pattern(reinterpret_cast<const BYTE*>(needle.c_str()), reinterpret_cast<const BYTE*>(needle.c_str() + needle.size()));
	ULONG64 found = 0;
	start = ::GetTickCount64();
	hr = co_await dbg.SearchMemory(co_await Eval(dbg, L"procs"), pattern, found);
	printf("  found at 0x%llX in %llu ms\n", found, ::GetTickCount64() - start);
	std::vector<BYTE> back;
	co_await dbg.ReadMemory(found, back, static_cast<ULONG>(pattern.size()));
	Check(SUCCEEDED(hr) && back == pattern, "SearchMemory finds the bytes");
	ULONG64 next = 0;
	hr = co_await dbg.SearchMemory(found + 1, pattern, next);
	Check(FAILED(hr) || next > found, "searching on from past it");
	std::vector<BYTE> nowhere{ 0xDE, 0xB0, 0x6E, 0x7E, 0x51, 0xAB, 0xC9, 0x12, 0x88, 0x4F, 0x02, 0x77 };
	start = ::GetTickCount64();
	hr = co_await dbg.SearchMemory(0, nowhere, next);
	printf("  not found in %llu ms\n", ::GetTickCount64() - start);
	Check(hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND), "bytes that are nowhere: ERROR_NOT_FOUND");

	// the child: the debugger stops at its start
	co_await dbg.ExecuteCommand(L"g");
	s = co_await Take(dbg);
	Check(s.Processes.size() == 2, "the child process is debugged too");
	auto current = std::find_if(s.Processes.begin(), s.Processes.end(), [](auto& p) { return p.Current; });
	auto other = std::find_if(s.Processes.begin(), s.Processes.end(), [](auto& p) { return !p.Current; });
	if (s.Processes.size() == 2 && current != s.Processes.end() && other != s.Processes.end()) {
		printf("  current %u (%ls), other %u (%ls)\n", current->ProcessId, current->ImagePath.c_str(), other->ProcessId, other->ImagePath.c_str());
		g_Events.ContextChanged = 0;
		hr = co_await dbg.SetCurrentProcess(other->Id);
		auto s2 = co_await Take(dbg);
		Check(SUCCEEDED(hr) && std::any_of(s2.Processes.begin(), s2.Processes.end(), [&](auto& p) { return p.Current && p.Id == other->Id; }),
			"SetCurrentProcess: the other one is current");
		Check(g_Events.ContextChanged >= 1, "ContextChanged after SetCurrentProcess");
	}

	co_await dbg.SetDebugChildProcesses(false);
	co_await dbg.Stop();
	::PostQuitMessage(0);
}
