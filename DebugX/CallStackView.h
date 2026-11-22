#pragma once

#include "ListPane.h"
#include "resource.h"

// The call stack of the current thread. (In the order of the frames: it is not sorted.) A double click makes a frame the
// scope (locals, registers and the disassembly are about it); the scope's frame has the arrow. The right-click menu
// also shows a frame's code in the Disassembly pane and runs to it (returns to it).
class CCallStackView : public CListPane<CCallStackView> {
public:
	void InitList();
	CString GetColumnText(HWND hWnd, int row, int column) const;
	int GetRowImage(HWND, int row, int col) const {
		return row < static_cast<int>(m_Items.size()) && m_Items[row].Index == DebugManager::Session().CurrentFrame() ? 1 : 0;
	}
	bool IsSortable(HWND, int) const {
		return false;
	}
	bool OnDoubleClickList(HWND, int row, int col, POINT const& pt);
	bool OnRightClickList(HWND, int row, int col, POINT const& pt);

	// the column widths kept before there was a Source column (6 of them) go to the columns they were for
	void LoadState(PCWSTR state) override;

	// ISessionObserver: another thread's stack, or another frame of it
	void OnContextChanged() override {
		RequestRefresh();
	}
	void OnScopeChanged() override {
		m_List.Invalidate();
	}
	// (a return address on the stack may have been)
	void OnMemoryChanged() override {
		RequestRefresh();
	}
	void OnTargetEnded() override;

	// CPaneBase
	DebugEngine::Task<void> Refresh();

private:
	enum Column { Index, Function, Source, Instruction, Return, Frame, Stack };

	void SwitchTo(int row);

	std::vector<DebugEngine::StackFrameInfo> m_Items;
	// where each frame is in the source (as m_Items): the line of the instruction of frame 0, and of the call of the others
	std::vector<DebugEngine::SourceLocation> m_Sources;
};
