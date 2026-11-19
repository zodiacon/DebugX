// Test target for threads and processes: a thread with a name, and a child process (procs.exe)
#include <Windows.h>
#include <cstdio>
#include <string>

static volatile LONG g_Ready;
static HANDLE g_PipeRead, g_PipeWrite;
static HANDLE g_Event;

DWORD WINAPI Worker(void*) {
	::SetThreadDescription(::GetCurrentThread(), L"Worker");
	::InterlockedExchange(&g_Ready, 1);
	// (a read that never ends: a handle whose name cannot be asked for without waiting, which the Handles pane must not do)
	char buffer[16];
	DWORD read;
	::ReadFile(g_PipeRead, buffer, sizeof(buffer), &read, nullptr);
	return 0;
}

int wmain(int argc, wchar_t* argv[]) {
	// the child: it only exits
	if (argc > 1 && wcscmp(argv[1], L"child") == 0)
		return 7;

	::SetThreadDescription(::GetCurrentThread(), L"Main Thread");
	::CreatePipe(&g_PipeRead, &g_PipeWrite, nullptr, 0);
	// (a named object, for the Handles pane)
	g_Event = ::CreateEventW(nullptr, TRUE, FALSE, L"DebugXTestEvent");
	::CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
	while (!g_Ready)
		::Sleep(10);
	::Sleep(100);		// (for the read to have started)
	// (the debugger looks at the threads here)
	::DebugBreak();

	wchar_t path[MAX_PATH];
	::GetModuleFileNameW(nullptr, path, _countof(path));
	std::wstring commandLine = L"\"" + std::wstring(path) + L"\" child";
	// (a console of its own, not shown: the system starts a console host for it, which the debugger lets go of)
	STARTUPINFOW si{ sizeof(si) };
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION pi;
	if (!::CreateProcessW(nullptr, &commandLine[0], nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &pi))
		return 1;
	::WaitForSingleObject(pi.hProcess, INFINITE);
	DWORD code = 0;
	::GetExitCodeProcess(pi.hProcess, &code);
	printf("child exited with %u\n", code);
	return 0;
}
