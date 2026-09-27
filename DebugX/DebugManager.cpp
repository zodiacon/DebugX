#include "stdafx.h"
#include "DebugManager.h"

using namespace DebugEngine;

DebugManager& DebugManager::Get() {
	static DebugManager mgr;
	return mgr;
}

HRESULT DebugManager::Init() {
	ATLASSERT(!m_Debugger);
	if (m_Debugger)
		return E_UNEXPECTED;

	auto debugger = std::make_unique<DebugEngine::DebuggerX>();
	auto session = std::make_unique<SessionModel>(*debugger);
	auto hr = debugger->Create([model = session.get()](auto type, void* data) {
		model->OnNotification(type, data);
		});
	if (SUCCEEDED(hr)) {
		m_Debugger = std::move(debugger);
		m_Session = std::move(session);
	}
	return hr;
}

void DebugManager::Destroy() {
	// the debugger first: it can still be delivering
	m_Debugger.reset();
	m_Session.reset();
}

DebugEngine::DebuggerX& DebugManager::DebuggerX() {
	ATLASSERT(Get().m_Debugger);
	return *Get().m_Debugger;
}

SessionModel& DebugManager::Session() {
	ATLASSERT(Get().m_Session);
	return *Get().m_Session;
}
