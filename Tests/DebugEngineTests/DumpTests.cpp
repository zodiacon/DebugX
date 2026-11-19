// Writing dump files of a live target, and opening one (loop.exe)
#include "Harness.h"

using namespace DebugEngine;

static ULONG64 FileSize(const std::wstring& path) {
	WIN32_FILE_ATTRIBUTE_DATA data;
	if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
		return 0;
	return (static_cast<ULONG64>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}

Task<void> DumpSuite(DebuggerX& dbg) {
	wchar_t temp[MAX_PATH];
	::GetTempPathW(_countof(temp), temp);
	auto mini = std::format(L"{}DebugEngineTests-{}-mini.dmp", temp, ::GetCurrentProcessId());
	auto full = std::format(L"{}DebugEngineTests-{}-full.dmp", temp, ::GetCurrentProcessId());

	auto hr = co_await dbg.LaunchAndAttach(Target(L"loop").c_str());
	Check(SUCCEEDED(hr), "launch loop.exe");
	co_await dbg.AddBreakpoint(L"`loop.cpp:9`");
	co_await dbg.ExecuteCommand(L"g");
	co_await dbg.ExecuteCommand(L"g");
	co_await dbg.ExecuteCommand(L"g");
	Check(co_await Eval(dbg, L"poi(loop!g_iter) & 0xffffffff") == 2, "stopped in the 3rd call (g_iter 2)");

	hr = co_await dbg.WriteDump(mini.c_str(), false);
	Check(SUCCEEDED(hr) && FileSize(mini) > 0, "a minidump written");
	hr = co_await dbg.WriteDump(full.c_str(), true);
	Check(SUCCEEDED(hr) && FileSize(full) > FileSize(mini), "a full dump written, bigger than the minidump");
	printf("  sizes: mini %llu, full %llu\n", FileSize(mini), FileSize(full));
	co_await dbg.Stop();

	// the full one has the memory: the global is there, and the thread stopped where it was
	hr = co_await dbg.OpenDumpFile(full.c_str());
	Check(SUCCEEDED(hr) && (dbg.GetTargetType() & TargetType::Dump) == TargetType::Dump, "the full dump opens");
	Check(co_await Eval(dbg, L"poi(loop!g_iter) & 0xffffffff") == 2, "the dump's g_iter is 2");
	auto loc = co_await Where(dbg);
	Check(loc.Line == 9, "the dump's thread is at line 9");

	// not of a dump
	hr = co_await dbg.WriteDump(mini.c_str(), false);
	Check(hr == E_NOTIMPL, "a dump of a dump: E_NOTIMPL");
	co_await dbg.Stop();

	::DeleteFileW(mini.c_str());
	::DeleteFileW(full.c_str());
	::PostQuitMessage(0);
}
