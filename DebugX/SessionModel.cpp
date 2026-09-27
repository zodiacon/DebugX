#include "stdafx.h"
#include "SessionModel.h"

using namespace DebugEngine;

SessionModel::SessionModel(DebuggerX& debugger) : m_Debugger(debugger) {
}

void SessionModel::AddObserver(ISessionObserver* observer) {
	if (std::find(m_Observers.begin(), m_Observers.end(), observer) == m_Observers.end())
		m_Observers.push_back(observer);
}

void SessionModel::RemoveObserver(ISessionObserver* observer) {
	std::erase(m_Observers, observer);
}

TargetType SessionModel::Target() const {
	return m_Debugger.GetTargetType();
}

bool SessionModel::IsBusy() const {
	return m_Debugger.IsBusy();
}

bool SessionModel::IsRunning() const {
	return m_Status == ExecutionStatus::Go || m_Status == ExecutionStatus::StepOver ||
		m_Status == ExecutionStatus::StepInfo || m_Status == ExecutionStatus::StepBranch;
}

bool SessionModel::CanRun() const {
	return IsBroken() && !IsBusy() && Target() != TargetType::LocalKernel;
}

// An observer can remove itself, or another one, while it handles an event.
template<typename F>
void SessionModel::Notify(F&& call) {
	auto observers = m_Observers;
	for (auto observer : observers) {
		if (std::find(m_Observers.begin(), m_Observers.end(), observer) != m_Observers.end())
			call(*observer);
	}
}

void SessionModel::OnNotification(DebuggerNotificationType type, void* data) {
	switch (type) {
		case DebuggerNotificationType::StatusChanged: {
			auto old = m_Status;
			m_Status = static_cast<ExecutionStatus>(*static_cast<ULONG*>(data));
			Notify([](auto& o) { o.OnSessionChanged(); });
			if (m_Status == ExecutionStatus::Break && old != ExecutionStatus::Break)
				Notify([](auto& o) { o.OnBroke(); });
			else if (old == ExecutionStatus::Break && m_Status != ExecutionStatus::Break && m_Status != ExecutionStatus::NoDebuggee)
				Notify([](auto& o) { o.OnResumed(); });
			break;
		}

		case DebuggerNotificationType::BusyChanged:
			Notify([](auto& o) { o.OnSessionChanged(); });
			break;

		case DebuggerNotificationType::TargetChanged: {
			auto hasTarget = HasTarget();
			bool changed = hasTarget != m_HadTarget;
			m_HadTarget = hasTarget;
			Notify([](auto& o) { o.OnSessionChanged(); });
			if (changed) {
				if (hasTarget)
					Notify([](auto& o) { o.OnTargetAttached(); });
				else
					Notify([](auto& o) { o.OnTargetEnded(); });
			}
			break;
		}

		case DebuggerNotificationType::ProcessCreated:
			Notify([data](auto& o) { o.OnProcessCreated(*static_cast<CreateProcessData*>(data)); });
			break;

		case DebuggerNotificationType::ExitProcess:
			Notify([data](auto& o) { o.OnProcessExited(*static_cast<ExitThreadData*>(data)); });
			break;

		case DebuggerNotificationType::ThreadCreated:
			Notify([data](auto& o) { o.OnThreadCreated(*static_cast<ThreadCreateData*>(data)); });
			break;

		case DebuggerNotificationType::ExitThread:
			Notify([data](auto& o) { o.OnThreadExited(*static_cast<ExitThreadData*>(data)); });
			break;

		case DebuggerNotificationType::Output:
			Notify([data](auto& o) { o.OnOutput(*static_cast<OutputData*>(data)); });
			break;
	}
}
