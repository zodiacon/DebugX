#pragma once

#include "ListPane.h"
#include "resource.h"

// The modules of the target: what is loaded where, and what the debugger knows about its symbols.
class CModulesView : public CListPane<CModulesView> {
public:
	void InitList();
	CString GetColumnText(HWND hWnd, int row, int column) const;
	int GetRowImage(HWND, int row, int col) const {
		return 0;
	}
	void DoSort(const SortInfo* si);

	// ISessionObserver: the list is up to date at the next stop
	void OnModuleLoaded(const DebugEngine::ModuleEventData&) override {
		RequestRefresh();
	}
	void OnModuleUnloaded(const DebugEngine::ModuleEventData&) override {
		RequestRefresh();
	}
	void OnTargetEnded() override;

	// CPaneBase
	DebugEngine::Task<void> Refresh();

private:
	std::vector<DebugEngine::ModuleInfo> m_Items;
};
