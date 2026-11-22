#pragma once

#include <memory>
#include "..\DebugEngine\Debugger.h"
#include "SessionModel.h"

//
// Owns the one debugger of the application (DbgEng's: DebuggerX) and the SessionModel that turns its notifications into events.
// Everything here happens on the UI thread: the debugger delivers its notifications there.
//
class DebugManager final {
public:
	static DebugManager& Get();

	HRESULT Init();
	void Destroy();

	static DebugEngine::IDebugger& Debugger();
	// the debugger as DbgEng's, for what only it can do (RunAsync); null if it is another engine
	static DebugEngine::DebuggerX* DbgEng();
	// DbgEng's RunAsync (the kernel analyzers); E_NOTIMPL with another engine
	static DebugEngine::Task<HRESULT> RunAsync(std::function<HRESULT(IDebugClient*)> func);
	static SessionModel& Session();
	// the dbgeng.dll in use (Init loads it)
	static const std::wstring& EnginePath() {
		return Get().m_EnginePath;
	}

	// Set Next Statement, from wherever it is asked for: moves the current thread to 'address', asking first (with 'owner'
	// as the message box's) if it is in another function. Beeps if the target cannot be changed now.
	static DebugEngine::Task<void> SetNextStatement(HWND owner, ULONG64 address);

	// Delete All / Enable All / Disable All Breakpoints (the Debug menu's, the Breakpoints pane's): every breakpoint of the
	// session's, whoever set it. Deleting asks first ('owner' the message box's). Beeps while the target runs (the engine
	// is not there to ask) or when there is none.
	static DebugEngine::Task<void> RemoveAllBreakpoints(HWND owner);
	static DebugEngine::Task<void> SetAllBreakpointsEnabled(bool enabled);
	// (what they need: a target that is not running)
	static bool CanChangeBreakpoints();

private:
	DebugManager() = default;
	DebugManager(const DebugManager&) = delete;
	DebugManager& operator=(const DebugManager&) = delete;

	std::unique_ptr<DebugEngine::IDebugger> m_Debugger;
	std::unique_ptr<SessionModel> m_Session;
	std::wstring m_EnginePath;
};
