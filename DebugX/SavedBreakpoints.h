#pragma once

#include <string>
#include <vector>
#include "Debugger.h"

// The breakpoints of each program debugged (by the full path of its executable, whether it was run or attached to),
// kept between sessions and runs of DebugX: set again when the program is next run or attached to (and by Restart).
// Code breakpoints only, not one-shot ones: a data breakpoint's address belongs to the process it was set in. In
// %LOCALAPPDATA%\DebugX\breakpoints.txt, for the programs debugged last (MaxPrograms).
namespace SavedBreakpoints {
	constexpr size_t MaxPrograms = 50;

	// the key of a program: its executable's full path, in lowercase; empty if there is none
	std::wstring KeyOf(const std::wstring& exePath);
	// the executable of a running process; empty if it cannot be known
	std::wstring ImagePathOf(DWORD pid);

	std::vector<DebugEngine::BreakpointSettings> Load(const std::wstring& key);
	// the program's breakpoints are these now (the ones that are not kept are left out); it becomes the latest one
	void Save(const std::wstring& key, const std::vector<DebugEngine::BreakpointInfo>& breakpoints);
	// the breakpoints that are kept of these
	bool IsKept(const DebugEngine::BreakpointSettings& bp);
}
