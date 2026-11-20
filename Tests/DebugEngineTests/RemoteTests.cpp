// A process server (the Windows Kits debugger's dbgsrv.exe, on this machine, over a named pipe): its processes, and a
// program launched there and debugged here (loop.exe)
#include "Harness.h"

using namespace DebugEngine;

Task<void> RemoteSuite(DebuggerX& dbg) {
	auto folder = WindowsKitsDebuggersFolder();
	auto dbgsrv = folder + L"dbgsrv.exe";
	if (folder.empty() || ::GetFileAttributesW(dbgsrv.c_str()) == INVALID_FILE_ATTRIBUTES) {
		printf("SKIP: no dbgsrv.exe (the Windows Kits debugger is not installed)\n");
		::PostQuitMessage(0);
		co_return;
	}

	// (a named pipe: no port for the firewall to ask about)
	auto pipe = std::format(L"DebugEngineTests{}", ::GetCurrentProcessId());
	auto commandLine = std::format(L"\"{}\" -t npipe:pipe={}", dbgsrv, pipe);
	STARTUPINFOW si{ sizeof(si) };
	PROCESS_INFORMATION pi{};
	Check(::CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi), "start dbgsrv.exe");

	auto options = std::format(L"npipe:server=localhost,pipe={}", pipe);
	HRESULT hr = E_FAIL;
	for (int i = 0; i < 50 && FAILED(hr); i++) {
		hr = co_await dbg.ConnectProcessServer(options.c_str());
		if (FAILED(hr))
			::Sleep(100);
	}
	if (FAILED(hr)) {
		// (the engine in use and dbgsrv.exe may not be of the same debugger)
		wchar_t engine[MAX_PATH]{};
		::GetModuleFileNameW(::GetModuleHandleW(L"dbgeng.dll"), engine, _countof(engine));
		const bool kits = _wcsnicmp(engine, folder.c_str(), folder.size()) == 0;
		printf("%s: ConnectProcessServer 0x%08X (engine %ls)\n", kits ? "FAIL" : "SKIP", static_cast<unsigned>(hr), engine);
		Check(!kits, "ConnectProcessServer: the engine of dbgsrv.exe's debugger");
		::TerminateProcess(pi.hProcess, 0);
		::CloseHandle(pi.hProcess);
		::CloseHandle(pi.hThread);
		::PostQuitMessage(0);
		co_return;
	}
	Check(SUCCEEDED(hr), "ConnectProcessServer");

	std::vector<ServerProcess> processes;
	hr = co_await dbg.EnumServerProcesses(processes);
	printf("  %zu processes on the server\n", processes.size());
	Check(SUCCEEDED(hr) && std::any_of(processes.begin(), processes.end(), [](auto& p) {
		return p.Pid == ::GetCurrentProcessId() && _wcsicmp(p.Name.c_str(), L"DebugEngineTests.exe") == 0;
		}), "the server's processes: this one among them, by name");

	// launched there, debugged here
	hr = co_await dbg.LaunchAndAttach(Target(L"loop").c_str());
	Check(SUCCEEDED(hr), "launch loop.exe on the process server");
	co_await dbg.AddBreakpoint(L"loop!Work");
	co_await dbg.ExecuteCommand(L"g");
	std::vector<StackFrameInfo> frames;
	co_await dbg.GetStackTrace(frames);
	Check(!frames.empty() && frames[0].Symbol.find(L"loop!Work") != std::wstring::npos, "stopped in loop!Work, with symbols");
	Check(co_await Eval(dbg, L"poi(loop!g_iter) & 0xffffffff") == 0, "its memory: g_iter is 0");
	std::vector<HandleInfo> handles;
	Check(co_await dbg.EnumHandles(handles) == E_NOTIMPL, "EnumHandles: not for a process server's target");

	hr = co_await dbg.DisconnectProcessServer();
	Check(hr == E_UNEXPECTED, "not disconnected while there is a target");
	co_await dbg.Stop();
	hr = co_await dbg.DisconnectProcessServer();
	Check(hr == S_OK, "DisconnectProcessServer");
	processes.clear();
	co_await dbg.EnumServerProcesses(processes);
	Check(std::any_of(processes.begin(), processes.end(), [](auto& p) { return p.Pid == ::GetCurrentProcessId(); }), "this machine's processes again");

	::TerminateProcess(pi.hProcess, 0);
	::CloseHandle(pi.hProcess);
	::CloseHandle(pi.hThread);
	::PostQuitMessage(0);
}
