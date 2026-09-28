#include "stdafx.h"
#include "PaneRegistry.h"
#include "CommandView.h"
#include "UserThreadsView.h"
#include "KernelProcessesView.h"
#include "ModulesView.h"
#include "RegistersView.h"
#include "CallStackView.h"
#include "BreakpointsView.h"
#include "OutputView.h"
#include "MemoryView.h"
#include "DisassemblyView.h"
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
		{ L"modules", L"Modules", IDI_DLL, ID_VIEW_MODULES, PaneKind::Tool, DockSide::Right, { 700, 300 }, TargetType::Unknown, true, L"registers", Make<CModulesView> },
		{ L"breakpoints", L"Breakpoints", IDI_BREAKPOINTS, ID_VIEW_BREAKPOINTS, PaneKind::Tool, DockSide::Bottom, { 700, 240 }, TargetType::Unknown, true, L"output", Make<CBreakpointsView> },
		{ L"processes", L"Processes", IDI_PROCESSES, ID_VIEW_PROCESSES, PaneKind::Tool, DockSide::Right, { 700, 300 }, TargetType::Kernel, true, L"registers", Make<CKernelProcessesView> },
		{ L"memory", L"Memory", IDI_MEMORY, ID_VIEW_MEMORY, PaneKind::Tool, DockSide::Bottom, { 700, 240 }, TargetType::Unknown, true, L"breakpoints", Make<CMemoryView> },
		{ L"disassembly", L"Disassembly", IDI_STRUCT, ID_VIEW_DISASSEMBLY, PaneKind::Tool, DockSide::Left, { 560, 300 }, TargetType::Unknown, true, L"callstack", Make<CDisassemblyView> },
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
