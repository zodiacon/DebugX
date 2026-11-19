#include "stdafx.h"
#include "PaneRegistry.h"
#include "CommandView.h"
#include "UserThreadsView.h"
#include "UserProcessesView.h"
#include "HandlesView.h"
#include "DriversView.h"
#include "ObjectsView.h"
#include "IrpsView.h"
#include "KernelProcessesView.h"
#include "KernelThreadsView.h"
#include "ModulesView.h"
#include "RegistersView.h"
#include "CallStackView.h"
#include "BreakpointsView.h"
#include "OutputView.h"
#include "MemoryView.h"
#include "DisassemblyView.h"
#include "LocalsView.h"
#include "WatchView.h"
#include "resource.h"

using namespace DebugEngine;
using namespace WTLDock;

namespace {
	template<typename T>
	IPane* Make() {
		return new T;
	}

	const PaneInfo s_Panes[] = {
		{ L"command", L"Command", IDI_COMMAND, ID_VIEW_COMMAND, PaneKind::Document, DockSide::Bottom, { 800, 300 }, TargetType::Unknown, false, nullptr, Make<CCommandView> },
		{ L"output", L"Output", IDI_EVENT, ID_VIEW_OUTPUT, PaneKind::Tool, DockSide::Bottom, { 800, 300 }, TargetType::Unknown, false, nullptr, Make<COutputView> },
		{ L"callstack", L"Call Stack", IDI_CALLSTACK, ID_VIEW_CALLSTACK, PaneKind::Tool, DockSide::Left, { 560, 300 }, TargetType::Unknown, true, nullptr, Make<CCallStackView> },
		{ L"registers", L"Registers", IDI_REGISTRY, ID_VIEW_REGISTERS, PaneKind::Tool, DockSide::Right, { 440, 300 }, TargetType::Unknown, true, nullptr, Make<CRegistersView> },
		{ L"threads", L"Threads", IDI_THREADS, ID_VIEW_THREADS, PaneKind::Tool, DockSide::Right, { 420, 300 }, TargetType::User, true, L"registers", Make<CUserThreadsView> },
		{ L"uprocesses", L"Processes", IDI_PROCESSES, ID_VIEW_USERPROCESSES, PaneKind::Tool, DockSide::Right, { 600, 300 }, TargetType::User, true, L"threads", Make<CUserProcessesView> },
		{ L"handles", L"Handles", IDI_TOOL, ID_VIEW_HANDLES, PaneKind::Tool, DockSide::Right, { 700, 300 }, TargetType::LiveUser, true, L"threads", Make<CHandlesView> },
		{ L"modules", L"Modules", IDI_DLL, ID_VIEW_MODULES, PaneKind::Tool, DockSide::Right, { 700, 300 }, TargetType::Unknown, true, L"registers", Make<CModulesView> },
		{ L"breakpoints", L"Breakpoints", IDI_BREAKPOINTS, ID_VIEW_BREAKPOINTS, PaneKind::Tool, DockSide::Bottom, { 700, 240 }, TargetType::Unknown, true, L"output", Make<CBreakpointsView> },
		{ L"processes", L"Kernel Processes", IDI_PROCESSES, ID_VIEW_PROCESSES, PaneKind::Tool, DockSide::Right, { 700, 300 }, TargetType::Kernel, true, L"registers", Make<CKernelProcessesView> },
		{ L"kthreads", L"Kernel Threads", IDI_THREADS, ID_VIEW_KERNELTHREADS, PaneKind::Tool, DockSide::Right, { 700, 300 }, TargetType::Kernel, true, L"processes", Make<CKernelThreadsView> },
		{ L"drivers", L"Drivers", IDI_DLL, ID_VIEW_DRIVERS, PaneKind::Tool, DockSide::Right, { 800, 300 }, TargetType::Kernel, true, L"processes", Make<CDriversView> },
		{ L"objects", L"Object Namespace", IDI_STRUCT, ID_VIEW_OBJECTS, PaneKind::Tool, DockSide::Right, { 700, 300 }, TargetType::Kernel, true, L"drivers", Make<CObjectsView> },
		{ L"irps", L"IRPs", IDI_EVENT, ID_VIEW_IRPS, PaneKind::Tool, DockSide::Right, { 900, 300 }, TargetType::Kernel, true, L"drivers", Make<CIrpsView> },
		{ L"memory", L"Memory", IDI_MEMORY, ID_VIEW_MEMORY, PaneKind::Tool, DockSide::Bottom, { 700, 240 }, TargetType::Unknown, true, L"breakpoints", Make<CMemoryView> },
		{ L"memory2", L"Memory 2", IDI_MEMORY, ID_VIEW_MEMORY2, PaneKind::Tool, DockSide::Bottom, { 700, 240 }, TargetType::Unknown, true, L"memory", Make<CMemoryView> },
		{ L"memory3", L"Memory 3", IDI_MEMORY, ID_VIEW_MEMORY3, PaneKind::Tool, DockSide::Bottom, { 700, 240 }, TargetType::Unknown, true, L"memory", Make<CMemoryView> },
		{ L"memory4", L"Memory 4", IDI_MEMORY, ID_VIEW_MEMORY4, PaneKind::Tool, DockSide::Bottom, { 700, 240 }, TargetType::Unknown, true, L"memory", Make<CMemoryView> },
		{ L"disassembly", L"Disassembly", IDI_STRUCT, ID_VIEW_DISASSEMBLY, PaneKind::Tool, DockSide::Left, { 560, 300 }, TargetType::Unknown, true, L"callstack", Make<CDisassemblyView> },
		{ L"locals", L"Locals", IDI_TOOL, ID_VIEW_LOCALS, PaneKind::Tool, DockSide::Bottom, { 700, 240 }, TargetType::Unknown, true, L"output", Make<CLocalsView> },
		{ L"watch", L"Watch", IDI_FIND, ID_VIEW_WATCH, PaneKind::Tool, DockSide::Bottom, { 700, 240 }, TargetType::Unknown, true, L"locals", Make<CWatchView> },
	};
}

PaneRegistry& PaneRegistry::Get() {
	static PaneRegistry registry;
	return registry;
}

void ForgetPane(IPane* pane) {
	PaneRegistry::Get().Forget(pane);
}

std::span<const PaneInfo> PaneRegistry::Panes() const {
	return s_Panes;
}

const PaneInfo* PaneRegistry::Find(std::wstring_view id) const {
	for (auto& info : Panes()) {
		if (id == info.Id)
			return &info;
	}
	return nullptr;
}

const PaneInfo* PaneRegistry::FindByCommand(UINT command) const {
	for (auto& info : Panes()) {
		if (info.Command == command)
			return &info;
	}
	return nullptr;
}

HWND PaneRegistry::CreatePane(const PaneInfo& info, HWND hParent) {
	std::unique_ptr<IPane> pane(info.Make());
	auto hWnd = pane->Create(hParent);
	if (!hWnd)
		return nullptr;

	// the window owns the pane from here (it deletes itself when the window is destroyed)
	auto p = pane.release();
	m_Live[info.Id] = p;
	if (auto it = m_States.find(info.Id); it != m_States.end())
		p->LoadState(it->second.c_str());
	return hWnd;
}

void PaneRegistry::Forget(IPane* pane) {
	for (auto it = m_Live.begin(); it != m_Live.end(); ++it) {
		if (it->second == pane) {
			m_Live.erase(it);
			return;
		}
	}
}

// one pane to a line: id=state
void PaneRegistry::LoadStates(const std::wstring& text) {
	m_States.clear();
	size_t pos = 0;
	while (pos < text.size()) {
		auto end = text.find(L'\n', pos);
		if (end == std::wstring::npos)
			end = text.size();
		auto line = std::wstring_view(text).substr(pos, end - pos);
		if (auto eq = line.find(L'='); eq != std::wstring_view::npos && eq > 0)
			m_States[std::wstring(line.substr(0, eq))] = std::wstring(line.substr(eq + 1));
		pos = end + 1;
	}
}

std::wstring PaneRegistry::SaveStates() const {
	auto states = m_States;
	for (auto& [id, pane] : m_Live)
		states[id] = pane->SaveState();

	std::wstring text;
	for (auto& [id, state] : states) {
		if (!state.empty())
			text += id + L'=' + state + L'\n';
	}
	return text;
}
