#include "stdafx.h"
#include "SessionModel.h"
#include <format>

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
	// (a local kernel never stops, a dump never runs)
	return IsBroken() && !IsBusy() && Target() != TargetType::LocalKernel && (Target() & TargetType::Dump) != TargetType::Dump;
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

void SessionModel::AddLogLine(std::wstring line) {
	FlushPartialLine();
	m_Log.push_back(line);
	if (m_Log.size() > MaxLogLines)
		m_Log.pop_front();
	Notify([&line](auto& o) { o.OnLogLine(line); });
}

void SessionModel::FlushPartialLine() {
	if (m_PartialLine.empty())
		return;

	auto line = std::move(m_PartialLine);
	m_PartialLine.clear();
	m_Log.push_back(line);
	if (m_Log.size() > MaxLogLines)
		m_Log.pop_front();
	Notify([&line](auto& o) { o.OnLogLine(line); });
}

// the target's text arrives in pieces that do not end where lines do
void SessionModel::AddDebuggeeText(const std::wstring& text) {
	m_PartialLine += text;
	size_t end;
	while ((end = m_PartialLine.find(L'\n')) != std::wstring::npos) {
		auto line = m_PartialLine.substr(0, end);
		m_PartialLine.erase(0, end + 1);
		while (!line.empty() && line.back() == L'\r')
			line.pop_back();
		AddLogLine(std::move(line));
	}
}

void SessionModel::OnNotification(DebuggerNotificationType type, void* data) {
	switch (type) {
		case DebuggerNotificationType::StatusChanged: {
			auto old = m_Status;
			m_Status = static_cast<ExecutionStatus>(*static_cast<ULONG*>(data));
			// (a stop is in frame 0, whatever was chosen before)
			if (m_Status == ExecutionStatus::Break && old != ExecutionStatus::Break)
				m_CurrentFrame = 0;
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
				else {
					m_CurrentThread = 0;
					m_CurrentFrame = 0;
					Notify([](auto& o) { o.OnTargetEnded(); });
				}
			}
			break;
		}

		case DebuggerNotificationType::ContextChanged:
			m_CurrentThread = static_cast<ContextData*>(data)->ThreadId;
			m_CurrentFrame = 0;
			Notify([](auto& o) { o.OnContextChanged(); });
			break;

		case DebuggerNotificationType::ScopeChanged: {
			// (the engine says so for every reset to frame 0 as well, and SetScopeFrame says it again: only a change counts)
			auto frame = static_cast<ScopeData*>(data)->FrameIndex;
			if (frame != m_CurrentFrame) {
				m_CurrentFrame = frame;
				Notify([](auto& o) { o.OnScopeChanged(); });
			}
			break;
		}

		case DebuggerNotificationType::MemoryChanged:
			Notify([](auto& o) { o.OnMemoryChanged(); });
			break;

		case DebuggerNotificationType::BreakpointsChanged:
			Notify([](auto& o) { o.OnBreakpointsChanged(); });
			break;

		case DebuggerNotificationType::ModuleLoaded: {
			auto& module = *static_cast<ModuleEventData*>(data);
			AddLogLine(std::format(L"Module loaded: {} at 0x{:X}", module.Name, module.BaseAddress));
			Notify([&module](auto& o) { o.OnModuleLoaded(module); });
			break;
		}

		case DebuggerNotificationType::ModuleUnloaded: {
			auto& module = *static_cast<ModuleEventData*>(data);
			AddLogLine(std::format(L"Module unloaded: {} at 0x{:X}", module.Name, module.BaseAddress));
			Notify([&module](auto& o) { o.OnModuleUnloaded(module); });
			break;
		}

		case DebuggerNotificationType::ProcessCreated: {
			auto& process = *static_cast<CreateProcessData*>(data);
			AddLogLine(std::format(L"Process created: {} at 0x{:X}", std::wstring(CA2W(process.ImageName.c_str())), reinterpret_cast<ULONG_PTR>(process.BaseAddress)));
			Notify([&process](auto& o) { o.OnProcessCreated(process); });
			break;
		}

		case DebuggerNotificationType::ExitProcess: {
			auto& process = *static_cast<ExitThreadData*>(data);
			AddLogLine(std::format(L"Process exited with code {} (0x{:X})", static_cast<int>(process.ExitCode), process.ExitCode));
			Notify([&process](auto& o) { o.OnProcessExited(process); });
			break;
		}

		case DebuggerNotificationType::ThreadCreated: {
			auto& thread = *static_cast<ThreadCreateData*>(data);
			AddLogLine(std::format(L"Thread {} created: TID {} (0x{:X}), start address 0x{:X}", thread.Id, thread.ThreadId, thread.ThreadId, thread.StartAddress));
			Notify([&thread](auto& o) { o.OnThreadCreated(thread); });
			break;
		}

		case DebuggerNotificationType::ExitThread: {
			auto& thread = *static_cast<ExitThreadData*>(data);
			AddLogLine(std::format(L"Thread {} exited: TID {} (0x{:X}), code {}", thread.Id, thread.ThreadId, thread.ThreadId, static_cast<int>(thread.ExitCode)));
			Notify([&thread](auto& o) { o.OnThreadExited(thread); });
			break;
		}

		case DebuggerNotificationType::Output: {
			auto& output = *static_cast<OutputData*>(data);
			if ((output.Mask & OutputDataMask::Debuggee) == OutputDataMask::Debuggee)
				AddDebuggeeText(output.Text);
			Notify([&output](auto& o) { o.OnOutput(output); });
			break;
		}
	}
}
