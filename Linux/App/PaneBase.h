#pragma once

#include <QWidget>
#include "Common.h"
#include "SessionModel.h"

//
// A pane that shows the target (CPaneBase on Windows): session events only mark it dirty, and it refreshes only when it
// is dirty, visible and CanRefresh() -- a hidden pane costs nothing while the target steps, and catches up when shown.
// Refresh() is a coroutine; the base calls it, and calls it again if the pane became dirty while it ran.
//
class PaneBase : public QWidget, public ISessionObserver {
public:
	explicit PaneBase(QWidget* parent = nullptr);
	~PaneBase() override;

	// (for the pane that has the focus: the main window's F9 / Ctrl+F10 / Ctrl+G / Ctrl+C go to it)
	virtual bool ToggleBreakpoint() { return false; }
	virtual bool RunToCursor() { return false; }
	virtual bool SetNextStatement() { return false; }
	virtual bool GoTo() { return false; }

protected:
	void MarkDirty();
	void TryRefresh();
	virtual DebugEngine::Task<void> Refresh() = 0;
	// the target is stopped (the default); a pane that can show something without one says so
	virtual bool CanRefresh() const;
	// the target has ended
	virtual void Clear() {}

	void showEvent(QShowEvent* event) override;

	void OnSessionChanged() override;
	void OnBroke() override { MarkDirty(); }
	void OnContextChanged() override { MarkDirty(); }
	void OnScopeChanged() override { MarkDirty(); }
	void OnMemoryChanged() override { MarkDirty(); }
	void OnTargetEnded() override { Clear(); MarkDirty(); }

	Lifetime m_Lifetime;

private:
	DebugEngine::Task<void> RunRefresh();

	bool m_Dirty{ true };
	bool m_Refreshing{ false };
	bool m_Scheduled{ false };
};
