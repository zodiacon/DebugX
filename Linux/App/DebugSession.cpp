#include "DebugSession.h"
#include "QtDispatcher.h"
#include "Common.h"
#include <QSettings>

DebugSession::DebugSession() : m_Debugger(std::make_unique<GdbEngine::GdbDebugger>()), m_Model(std::make_unique<SessionModel>(*m_Debugger)) {
	s_Instance = this;
}

DebugSession::~DebugSession() {
	// the debugger first (it stops GDB, which ends the target), then the model it notified
	m_Debugger.reset();
	m_Model.reset();
	s_Instance = nullptr;
}

bool DebugSession::Init() {
	auto gdb = QSettings().value("gdbPath").toString();
	if (!gdb.isEmpty())
		m_Debugger->SetGdbPath(gdb.toStdString());
	return SUCCEEDED(m_Debugger->Create([this](DebugEngine::DebuggerNotificationType type, void* data) {
		m_Model->OnNotification(type, data);
	}, std::make_unique<QtDispatcher>()));
}

GdbEngine::GdbDebugger& Debugger() {
	return DebugSession::Instance()->Debugger();
}

SessionModel& Session() {
	return DebugSession::Instance()->Model();
}
