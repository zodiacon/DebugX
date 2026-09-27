#pragma once

#include <map>
#include <span>
#include <string>
#include <string_view>
#include "PaneBase.h"

//
// The tool windows of the application. Static facts about a pane (what it is called, where it goes the first time,
// what a target has to be like for it to make sense) are in its PaneInfo; the pane itself, and its window, are made
// when it is first shown and live as long as the frame does.
//
struct PaneInfo {
	PCWSTR Id;				// stable: the saved layout and the saved state of the pane refer to it
	PCWSTR Title;
	UINT Icon;
	UINT Command;			// the View menu's
	WTLDock::DockSide Side;	// where it goes the first time it is shown
	SIZE Size;
	// the flags a target must have (User, Kernel, ...) for the pane to be of use; none: always
	DebugEngine::TargetType Requires;
	IPane* (*Make)();
};

class PaneRegistry final {
public:
	static PaneRegistry& Get();

	std::span<const PaneInfo> Panes() const;
	const PaneInfo* Find(std::wstring_view id) const;
	const PaneInfo* FindByCommand(UINT command) const;

	static bool IsAvailable(const PaneInfo& info, DebugEngine::TargetType target) {
		return (target & info.Requires) == info.Requires;
	}

	// makes the pane and its window (as a child of hParent) and gives it the state that was kept; null if that fails
	HWND CreatePane(const PaneInfo& info, HWND hParent);
	void Forget(IPane* pane);

	// The state of the panes as text, for the settings: what the panes that exist say, and what was kept for the
	// ones that were never made in this run.
	void LoadStates(const std::wstring& text);
	std::wstring SaveStates() const;

private:
	PaneRegistry() = default;

	std::map<std::wstring, IPane*, std::less<>> m_Live;
	std::map<std::wstring, std::wstring, std::less<>> m_States;
};
