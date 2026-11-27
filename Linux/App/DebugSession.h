#pragma once

#include <memory>
#include "GdbDebugger.h"
#include "SessionModel.h"

//
// The debugger and its SessionModel, for the lifetime of the application (the Linux counterpart of DebugManager): the
// debugger's notifications go to the model, which turns them into events for the windows (ISessionObserver).
//
class DebugSession final {
public:
	DebugSession();
	~DebugSession();

	bool Init();

	GdbEngine::GdbDebugger& Debugger() {
		return *m_Debugger;
	}
	SessionModel& Model() {
		return *m_Model;
	}

	static DebugSession* Instance() {
		return s_Instance;
	}

private:
	std::unique_ptr<GdbEngine::GdbDebugger> m_Debugger;
	std::unique_ptr<SessionModel> m_Model;
	static inline DebugSession* s_Instance;
};
