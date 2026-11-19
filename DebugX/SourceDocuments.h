#pragma once

#include <string>
#include <string_view>
#include "DebugManager.h"
#include "Lifetime.h"

namespace WTLDock {
	class CDockHost;
	class DockPane;
	class DockLayout;
}
class CSourceView;

//
// The source files, as documents in the dock (a pane each, "source:<path>", made when a file is first opened and
// removed when it is closed; the layout keeps the ones that were open, so they come back with it). Whenever the target
// stops, or another thread or frame is chosen, it works out where the scope frame is in the source, opens that file
// (without taking the focus) and puts the arrows on the lines; where there is no source the status bar says why.
//
class SourceDocuments final : public ISessionObserver {
public:
	void Init(WTLDock::CDockHost& dock, HWND statusBar);
	void Term();

	static bool IsSourceId(std::wstring_view id);
	// the dock's factories: the window of a source pane, and a source pane that a saved layout names
	HWND CreateView(const WTLDock::DockPane& pane, HWND parent);
	WTLDock::DockPane* MakePane(WTLDock::DockLayout& layout, const std::wstring& id);
	// a source pane was closed: it is done with (its window and the pane go)
	void OnPaneClosed(WTLDock::DockPane* pane);

	// Shows a file (opening it if it is not), with the caret on 'line' if not 0; 'activate' moves the focus to it.
	CSourceView* Open(const std::wstring& path, ULONG line, bool activate);

	// ISessionObserver
	void OnBroke() override {
		RequestUpdate();
	}
	void OnContextChanged() override {
		RequestUpdate();
	}
	void OnScopeChanged() override {
		RequestUpdate();
	}
	void OnResumed() override;
	void OnTargetEnded() override;

private:
	WTLDock::DockPane* FindPane(const std::wstring& path) const;
	CSourceView* FindView(const std::wstring& path) const;
	void RequestUpdate();
	DebugEngine::Task<void> Update();
	void SetStatus(const std::wstring& text);

	WTLDock::CDockHost* m_Dock{};
	HWND m_StatusBar{};
	HICON m_Icon{};
	bool m_Updating{ false }, m_UpdatePending{ false };
	Lifetime m_Lifetime;
};
