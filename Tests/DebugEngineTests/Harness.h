#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <DbgEng.h>
#include <atlbase.h>
#include <cstdio>
#include <format>
#include <string>
#include <vector>
#include <algorithm>
#include "Debugger.h"

//
// The tests drive DebugEngine::DebuggerX against small programs (Tests\Targets, built next to the runner in Tests\) from
// a coroutine, the way DebugX's windows do: each suite runs on the UI thread's message loop, with a debugger of its
// own, and posts WM_QUIT when it is done.
//

// what the debugger told the notification handler; reset for each suite
struct Notifications {
	std::vector<ULONG> Scopes;		// ScopeChanged frame indices
	std::vector<ULONG> Exceptions;	// Exception codes
	int MemoryChanged{ 0 };
	int ContextChanged{ 0 };
	DebugEngine::ExecutionStatus Status{ DebugEngine::ExecutionStatus::NoDebuggee };	// the last StatusChanged's
};
extern Notifications g_Events;

void Check(bool ok, const char* what);

// the full path of a test target ("loop": ...\Tests\loop.exe)
std::wstring Target(PCWSTR name);

DebugEngine::Task<std::vector<DebugEngine::BreakpointInfo>> Breakpoints(DebugEngine::DebuggerX& dbg);
// an expression's value; ~0 if it does not evaluate
DebugEngine::Task<ULONG64> Eval(DebugEngine::DebuggerX& dbg, PCWSTR expression);
// where the current thread is in the source (and it is printed)
DebugEngine::Task<DebugEngine::SourceLocation> Where(DebugEngine::DebuggerX& dbg);
// the engine's code level (DEBUG_LEVEL_SOURCE / ASSEMBLY)
DebugEngine::Task<ULONG> CodeLevel(DebugEngine::DebuggerX& dbg);
bool EndsWith(const std::wstring& s, const wchar_t* tail);

bool Has(const std::vector<DebugEngine::SymbolEntry>& entries, const wchar_t* name, ULONG depth = 0);
void Dump(const char* title, const std::vector<DebugEngine::SymbolEntry>& entries);

// the suites; each ends with PostQuitMessage
DebugEngine::Task<void> ScopeSuite(DebugEngine::DebuggerX& dbg);
DebugEngine::Task<void> BreakpointSuite(DebugEngine::DebuggerX& dbg);
DebugEngine::Task<void> ExceptionSuite(DebugEngine::DebuggerX& dbg);
DebugEngine::Task<void> SourceSuite(DebugEngine::DebuggerX& dbg);
DebugEngine::Task<void> NextStatementSuite(DebugEngine::DebuggerX& dbg);
DebugEngine::Task<void> DumpSuite(DebugEngine::DebuggerX& dbg);
DebugEngine::Task<void> KernelSuite(DebugEngine::DebuggerX& dbg);
DebugEngine::Task<void> ValueSuite(DebugEngine::DebuggerX& dbg);
DebugEngine::Task<void> ProcessSuite(DebugEngine::DebuggerX& dbg);
DebugEngine::Task<void> RemoteSuite(DebugEngine::DebuggerX& dbg);
DebugEngine::Task<void> ScriptSuite(DebugEngine::DebuggerX& dbg);
DebugEngine::Task<void> McpSuite(DebugEngine::DebuggerX& dbg);
DebugEngine::Task<void> AssistantSuite(DebugEngine::DebuggerX& dbg);
