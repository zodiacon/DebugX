#pragma once

#include "ListPane.h"
#include "resource.h"

// The object manager's namespace of a kernel target, a directory at a time (KernelObjectAnalyzer): \, \Driver,
// \Device, ... The first row is the directory's path (double-click it to go up); double-click a directory to go into
// it, a symbolic link to go where it points. The directory shown is kept with the pane.
class CObjectsView : public CListPane<CObjectsView> {
public:
	void InitList();
	CString GetColumnText(HWND hWnd, int row, int column) const;
	int GetRowImage(HWND, int, int) const {
		return -1;
	}
	void DoSort(const SortInfo* si);
	bool OnDoubleClickList(HWND, int row, int col, POINT const& pt);
	bool OnRightClickList(HWND, int row, int col, POINT const& pt);

	void OnTargetEnded() override;

	// IPane: the column widths and the directory
	std::wstring SaveState() const override;
	void LoadState(PCWSTR state) override;

	// CPaneBase
	bool CanRefresh() const;
	DebugEngine::Task<void> Refresh();

	struct Object {
		ULONG64 Address;
		std::wstring Name, Type, LinkTarget;
	};

private:
	void GoTo(std::wstring path);
	// (the row for the directory itself, before the objects)
	static constexpr int HeaderRows = 1;

	std::wstring m_Path{ L"\\" };
	std::wstring m_Error;		// why the directory could not be shown
	std::vector<Object> m_Items;
};
