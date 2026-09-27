#include "stdafx.h"
#include "PaneRegistry.h"
#include "CommandView.h"
#include "UserThreadsView.h"
#include "KernelProcessesView.h"
#include "resource.h"

using namespace DebugEngine;
using namespace WTLDock;

namespace {
	template<typename T>
	IPane* Make() {
		return new T;
	}

	const PaneInfo s_Panes[] = {
		{ L"command", L"Command", IDI_COMMAND, ID_VIEW_COMMAND, DockSide::Bottom, { 800, 300 }, TargetType::Unknown, Make<CCommandView> },
		{ L"threads", L"Threads", IDI_THREADS, ID_VIEW_THREADS, DockSide::Right, { 420, 300 }, TargetType::User, Make<CUserThreadsView> },
		{ L"processes", L"Processes", IDI_PROCESSES, ID_VIEW_PROCESSES, DockSide::Right, { 700, 300 }, TargetType::Kernel, Make<CKernelProcessesView> },
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
