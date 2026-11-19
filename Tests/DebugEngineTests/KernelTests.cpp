// Kernel processes and threads, on the local kernel (needs an elevated runner and a system booted with /debug: skipped
// otherwise). The process looked at is the runner's own: what is seen of it can be checked from the inside.
#include "Harness.h"
#include <atlstr.h>
#include "DebugX.Kernel.h"

using namespace DebugEngine;
using namespace DebugX::Analysis;

// what the debugger reads of this process's memory, through the kernel
static volatile ULONG64 g_Marker = 0x0DEB06E7E5715ULL;

namespace {
	struct KernelThread {
		ULONG64 Address;
		ULONG Tid;
		ULONG64 Start;
	};
}

Task<void> KernelSuite(DebuggerX& dbg) {
	auto hr = co_await dbg.AttachLocalKernel();
	if (FAILED(hr)) {
		printf("SKIP: the local kernel cannot be attached to (0x%08X): an elevated runner and bcdedit /debug on are needed\n", hr);
		::PostQuitMessage(0);
		co_return;
	}
	Check(dbg.GetTargetType() == TargetType::LocalKernel, "attached to the local kernel");

	// the kernel's symbols (its types: EPROCESS, ETHREAD) come from Microsoft's symbol server, if no path says otherwise
	std::wstring symbolPath, sourcePath;
	co_await dbg.GetSymbolPaths(symbolPath, sourcePath);
	if (symbolPath.empty()) {
		std::wstring cache = L"C:\\Symbols";
		if (::GetFileAttributesW(cache.c_str()) == INVALID_FILE_ATTRIBUTES) {
			wchar_t temp[MAX_PATH];
			::GetTempPathW(_countof(temp), temp);
			cache = std::wstring(temp) + L"DebugEngineTests.Symbols";
		}
		co_await dbg.SetSymbolPath((L"srv*" + cache + L"*https://msdl.microsoft.com/download/symbols").c_str());
		co_await dbg.ReloadSymbols();
	}
	// (a symbol server needs symsrv.dll next to the engine's dbghelp.dll: without it only a symbol path of plain folders
	// with the kernel's PDB in them works)
	if (co_await Eval(dbg, L"nt!PsActiveProcessHead") == ~0ull) {
		printf("SKIP: no symbols for the kernel (no symsrv.dll next to the engine's dbghelp.dll, or not on the symbol path)\n");
		co_await dbg.Detach();
		::PostQuitMessage(0);
		co_return;
	}

	// this process's EPROCESS, and its threads
	ULONG64 self = 0;
	std::vector<KernelThread> threads;
	const auto pid = ::GetCurrentProcessId();
	hr = co_await dbg.RunAsync([&](IDebugClient* client) -> HRESULT {
		KernelProcessEnumerator processes(client);
		while (auto p = processes.GetNextProcess()) {
			if (p->GetProcessId() != pid)
				continue;
			self = p->GetProcessAddress();
			auto enumerator = p->GetThreadEnumerator();
			while (auto t = enumerator->GetNextThread())
				threads.push_back({ t->GetAddress(), t->GetClientId().ThreadId, t->GetStartAddress() });
			break;
		}
		return self ? S_OK : E_FAIL;
		});
	Check(SUCCEEDED(hr) && self != 0, "this process found among the kernel's (EPROCESS)");
	printf("  EPROCESS 0x%llX, %zu threads\n", self, threads.size());
	Check(std::any_of(threads.begin(), threads.end(), [](auto& t) { return t.Tid == ::GetCurrentThreadId(); }), "its threads include this one");
	Check(std::all_of(threads.begin(), threads.end(), [](auto& t) { return t.Start != 0; }), "every thread has a start address");

	// .process /p /r: its memory and its modules. (On the local kernel the process looked at is the debugger's own at first
	// -- this one: the System process is gone to first.)
	auto system = co_await Eval(dbg, L"poi(nt!PsInitialSystemProcess)");
	hr = co_await dbg.SetKernelProcess(system);
	ULONG64 now = 0;
	co_await dbg.GetKernelProcess(now);
	Check(SUCCEEDED(hr) && now == system && system != self, "SetKernelProcess: the System process");
	hr = co_await dbg.SetKernelProcess(self);
	Check(SUCCEEDED(hr), "SetKernelProcess: this one");
	co_await dbg.GetKernelProcess(now);
	Check(now == self, "GetKernelProcess: this process now");
	std::vector<BYTE> bytes;
	co_await dbg.ReadMemory(reinterpret_cast<ULONG64>(&g_Marker), bytes, sizeof(g_Marker));
	Check(bytes.size() == sizeof(g_Marker) && *reinterpret_cast<ULONG64*>(bytes.data()) == g_Marker, "its user-mode memory reads as it is");
	std::vector<ModuleInfo> modules;
	co_await dbg.EnumModules(modules);
	Check(std::any_of(modules.begin(), modules.end(), [](auto& m) { return _wcsicmp(m.Name.c_str(), L"DebugEngineTests") == 0; }),
		"its user-mode modules are listed");

	hr = co_await dbg.SetKernelProcess(self, true);
	Check(hr == E_NOTIMPL, "invasive (.process /i) is for a kernel over a connection only");

	// .thread /p /r: the thread the debugger looks at
	auto self_thread = std::find_if(threads.begin(), threads.end(), [](auto& t) { return t.Tid == ::GetCurrentThreadId(); });
	if (self_thread != threads.end()) {
		hr = co_await dbg.SetKernelThread(self_thread->Address);
		Check(SUCCEEDED(hr), "SetKernelThread: this thread");
		Check(co_await Eval(dbg, L"@$thread") == self_thread->Address, "@$thread is it");
	}

	co_await dbg.Detach();
	::PostQuitMessage(0);
}
