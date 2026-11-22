#pragma once

#include "ListPane.h"
#include "resource.h"

// The drivers of a kernel target: the driver objects in \Driver and \FileSystem (KernelObjectAnalyzer), with their
// images, devices and unload routines. Right-click: the dispatch routines (!drvobj), Details (dt nt!_DRIVER_OBJECT).
class CDriversView : public CListPane<CDriversView> {
public:
	void InitList();
	CString GetColumnText(HWND hWnd, int row, int column) const;
	int GetRowImage(HWND, int, int) const {
		return 0;
	}
	void DoSort(const SortInfo* si);
	bool HasFilter() const {
		return true;
	}
	bool OnDoubleClickList(HWND, int row, int col, POINT const& pt);
	bool OnRightClickList(HWND, int row, int col, POINT const& pt);

	void OnTargetEnded() override;

	// CPaneBase
	bool CanRefresh() const;
	DebugEngine::Task<void> Refresh();

	struct Driver {
		ULONG64 Address;
		std::wstring Name;
		std::wstring Image;		// the module at its start
		ULONG64 Start;
		ULONG Size;
		ULONG Devices;
		std::wstring Unload;	// the symbol of its unload routine
	};

private:
	std::vector<Driver> m_Items;
};
