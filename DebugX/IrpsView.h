#pragma once

#include "ListPane.h"
#include "resource.h"

// The IRPs not complete yet of the threads of the kernel process the debugger looks at (Kernel Processes > Switch to
// Process), or of all processes (right-click; kept with the pane): who issued it, what it is (the major function of
// the driver that has it now), the driver, the file. Double-click: dt nt!_IRP; right-click: !irp.
class CIrpsView : public CListPane<CIrpsView> {
public:
	void InitList();
	CString GetColumnText(HWND hWnd, int row, int column) const;
	int GetRowImage(HWND, int, int) const {
		return -1;
	}
	void DoSort(const SortInfo* si);
	bool OnDoubleClickList(HWND, int row, int col, POINT const& pt);
	bool OnRightClickList(HWND, int row, int col, POINT const& pt);

	// ISessionObserver: another process looked at
	void OnContextChanged() override {
		RequestRefresh();
	}
	void OnTargetEnded() override;

	// IPane: the column widths and whether all processes' are shown
	std::wstring SaveState() const override;
	void LoadState(PCWSTR state) override;

	// CPaneBase
	bool CanRefresh() const;
	DebugEngine::Task<void> Refresh();

	struct Irp {
		ULONG64 Address;
		ULONG ProcessId, ThreadId;
		std::wstring Process;
		UCHAR Major, Minor;
		std::wstring Driver;
		std::wstring File;
		ULONG Flags;
	};

private:
	bool m_AllProcesses{ false };
	std::vector<Irp> m_Items;
};
