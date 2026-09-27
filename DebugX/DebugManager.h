#pragma once

#include <memory>
#include "..\DebugEngine\Debugger.h"
#include "SessionModel.h"

//
// Owns the one DebuggerX of the application and the SessionModel that turns its notifications into events.
// Everything here happens on the UI thread: the debugger delivers its notifications there.
//
class DebugManager final {
public:
	static DebugManager& Get();

	HRESULT Init();
	void Destroy();

	static DebugEngine::DebuggerX& DebuggerX();
	static SessionModel& Session();

private:
	DebugManager() = default;
	DebugManager(const DebugManager&) = delete;
	DebugManager& operator=(const DebugManager&) = delete;

	std::unique_ptr<DebugEngine::DebuggerX> m_Debugger;
	std::unique_ptr<SessionModel> m_Session;
};
