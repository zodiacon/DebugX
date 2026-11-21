#include "pch.h"
#include "CallStackView.h"
#include "DebugManager.h"

void CCallStackView::InitList() {
	m_List.InsertColumn(0, L"#", LVCFMT_RIGHT, 40);
	m_List.InsertColumn(1, L"Function", LVCFMT_LEFT, 380);
	m_List.InsertColumn(2, L"Instruction", LVCFMT_RIGHT, 140);
	m_List.InsertColumn(3, L"Return", LVCFMT_RIGHT, 140);
	m_List.InsertColumn(4, L"Frame", LVCFMT_RIGHT, 140);
	m_List.InsertColumn(5, L"Stack", LVCFMT_RIGHT, 140);

	// a frame, and the one the thread is in
	CImageList images;
	images.Create(16, 16, ILC_COLOR32, 2, 0);
	images.AddIcon(AtlLoadIconImage(IDI_CALLSTACK, 0, 16, 16));
	images.AddIcon(AtlLoadIconImage(IDI_ICON9, 0, 16, 16));
	m_List.SetImageList(images, LVSIL_SMALL);
}

CString CCallStackView::GetColumnText(HWND hWnd, int row, int column) const {
	auto& f = m_Items[row];
	CString text;

	switch (column) {
		case 0: text.Format(L"%u", f.Index); break;
		case 1:
			if (!f.Symbol.empty())
				return f.Symbol.c_str();
			text.Format(L"0x%llX", f.InstructionOffset);
			break;
		case 2: text.Format(L"0x%016llX", f.InstructionOffset); break;
		case 3: text.Format(L"0x%016llX", f.ReturnOffset); break;
		case 4: text.Format(L"0x%016llX", f.FrameOffset); break;
		case 5: text.Format(L"0x%016llX", f.StackOffset); break;
	}
	return text;
}

bool CCallStackView::OnDoubleClickList(HWND, int row, int col, POINT const& pt) {
	// (a dump too: it is always stopped)
	if (row < 0 || row >= static_cast<int>(m_Items.size()) || !DebugManager::Session().IsBroken())
		return false;

	// (the panes that follow the scope, this one included, hear of it from the session)
	DebugManager::DebuggerX().SetScopeFrame(m_Items[row].Index);
	return true;
}

void CCallStackView::OnTargetEnded() {
	m_Items.clear();
	UpdateList(0);
	MarkDirty();
}

DebugEngine::Task<void> CCallStackView::Refresh() {
	auto alive = m_Lifetime.Watch();

	std::vector<DebugEngine::StackFrameInfo> frames;
	co_await DebugManager::DebuggerX().GetStackTrace(frames);
	if (!*alive)
		co_return;

	m_Items.swap(frames);
	UpdateList(m_Items.size());
	RefreshDone();
}
