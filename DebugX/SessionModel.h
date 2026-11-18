#pragma once

#include <deque>
#include <string>
#include <vector>
#include "..\DebugEngine\Debugger.h"

//
// What the windows of the application want to know about the debugging session, as events: they implement the
// members they care about and register with the SessionModel. All of it happens on the UI thread.
//
struct ISessionObserver {
	virtual ~ISessionObserver() = default;

	// Anything the state of the UI is derived from has changed: the execution status, whether a call to the
	// engine is in flight, the target. The model has the new state (a window's controls follow it from here).
	virtual void OnSessionChanged() {}

	virtual void OnTargetAttached() {}
	virtual void OnTargetEnded() {}
	// the target has stopped: a breakpoint, an exception, a step, Break
	virtual void OnBroke() {}
	virtual void OnResumed() {}

	// the current thread is another one, or its registers were written to
	virtual void OnContextChanged() {}
	// another frame of the current thread's stack was chosen (SessionModel::CurrentFrame) -- not when the target stops
	// or the thread changes, which put it back to 0 and have events of their own
	virtual void OnScopeChanged() {}
	virtual void OnBreakpointsChanged() {}
	virtual void OnModuleLoaded(const DebugEngine::ModuleEventData& data) {}
	virtual void OnModuleUnloaded(const DebugEngine::ModuleEventData& data) {}

	virtual void OnProcessCreated(const DebugEngine::CreateProcessData& data) {}
	virtual void OnProcessExited(const DebugEngine::ExitThreadData& data) {}
	virtual void OnThreadCreated(const DebugEngine::ThreadCreateData& data) {}
	virtual void OnThreadExited(const DebugEngine::ExitThreadData& data) {}
	virtual void OnOutput(const DebugEngine::OutputData& data) {}

	// a line was added to the log of what happens in the target (SessionModel::Log)
	virtual void OnLogLine(const std::wstring& line) {}
};

//
// Turns the debugger's notifications into the events above, and answers what state the session is in. The events
// come in the order the engine reported them, so a status seen here is never ahead of the events that precede it.
//
class SessionModel final {
public:
	explicit SessionModel(DebugEngine::DebuggerX& debugger);
	SessionModel(const SessionModel&) = delete;
	SessionModel& operator=(const SessionModel&) = delete;

	void AddObserver(ISessionObserver* observer);
	void RemoveObserver(ISessionObserver* observer);

	DebugEngine::ExecutionStatus Status() const {
		return m_Status;
	}
	DebugEngine::TargetType Target() const;
	// a call to the engine is in flight
	bool IsBusy() const;

	bool HasTarget() const {
		return Target() != DebugEngine::TargetType::Unknown;
	}
	bool IsBroken() const {
		return m_Status == DebugEngine::ExecutionStatus::Break;
	}
	bool IsRunning() const;
	// the target is stopped and nothing else is going on: it can be run, stepped, asked things
	bool CanRun() const;

	// the engine's id of the current thread (DebugEngine::ThreadInfo::Id), as far as it has said
	ULONG CurrentThreadId() const {
		return m_CurrentThread;
	}
	// the frame of the current thread's stack that locals and registers are about (StackFrameInfo::Index)
	ULONG CurrentFrame() const {
		return m_CurrentFrame;
	}

	// What happened in the target, a line each (processes and threads coming and going, modules loading, what the
	// target writes with OutputDebugString), the last ones. It is kept here, not by the window that shows it, so that
	// the window is complete whenever it is made.
	const std::deque<std::wstring>& Log() const {
		return m_Log;
	}

	// from DebugManager, for every notification of the debugger
	void OnNotification(DebugEngine::DebuggerNotificationType type, void* data);

private:
	template<typename F>
	void Notify(F&& call);
	void AddLogLine(std::wstring line);
	void AddDebuggeeText(const std::wstring& text);
	void FlushPartialLine();

	static constexpr size_t MaxLogLines = 5000;

	DebugEngine::DebuggerX& m_Debugger;
	std::vector<ISessionObserver*> m_Observers;
	DebugEngine::ExecutionStatus m_Status{ DebugEngine::ExecutionStatus::NoDebuggee };
	bool m_HadTarget{ false };
	ULONG m_CurrentThread{ 0 };
	ULONG m_CurrentFrame{ 0 };
	std::deque<std::wstring> m_Log;
	std::wstring m_PartialLine;		// text of the target that has not reached the end of its line
};
