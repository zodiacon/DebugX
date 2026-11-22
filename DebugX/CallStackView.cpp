#include "pch.h"
#include "CallStackView.h"
#include "DebugManager.h"
#include "AppMessages.h"
#include <format>

void CCallStackView::InitList() {
	m_List.InsertColumn(Index, L"#", LVCFMT_RIGHT, 40);
	m_List.InsertColumn(Function, L"Function", LVCFMT_LEFT, 380);
	m_List.InsertColumn(Source, L"Source", LVCFMT_LEFT, 220);
	m_List.InsertColumn(Instruction, L"Instruction", LVCFMT_RIGHT, 140);
	m_List.InsertColumn(Return, L"Return", LVCFMT_RIGHT, 140);
	m_List.InsertColumn(Frame, L"Frame", LVCFMT_RIGHT, 140);
	m_List.InsertColumn(Stack, L"Stack", LVCFMT_RIGHT, 140);

	// a frame, and the one the thread is in
	CImageList images;
	images.Create(16, 16, ILC_COLOR32, 2, 0);
	images.AddIcon(AtlLoadIconImage(IDI_CALLSTACK, 0, 16, 16));
	images.AddIcon(AtlLoadIconImage(IDI_ICON9, 0, 16, 16));
	m_List.SetImageList(images, LVSIL_SMALL);
}

void CCallStackView::LoadState(PCWSTR state) {
	std::wstring text(state ? state : L"");
	if (text.starts_with(L"w:") && std::count(text.begin(), text.end(), L',') == 5) {
		// w:#,Function,Instruction,... -> w:#,Function,<the default>,Instruction,...
		auto second = text.find(L',', text.find(L',') + 1);
		text.insert(second + 1, std::to_wstring(220) + L',');
	}
	CListPane::LoadState(text.c_str());
}

CString CCallStackView::GetColumnText(HWND hWnd, int row, int column) const {
	auto& f = m_Items[row];
	CString text;

	switch (column) {
		case Index: text.Format(L"%u", f.Index); break;
		case Function:
			if (!f.Symbol.empty())
				return f.Symbol.c_str();
			text.Format(L"0x%llX", f.InstructionOffset);
			break;
		case Source:
			if (row < static_cast<int>(m_Sources.size()) && !m_Sources[row].File.empty()) {
				// (the file's name: the path is in the source view's tab when it is opened)
				auto& s = m_Sources[row];
				text.Format(L"%s:%u", s.File.substr(s.File.find_last_of(L"\\/") + 1).c_str(), s.Line);
			}
			break;
		case Instruction: text.Format(L"0x%016llX", f.InstructionOffset); break;
		case Return: text.Format(L"0x%016llX", f.ReturnOffset); break;
		case Frame: text.Format(L"0x%016llX", f.FrameOffset); break;
		case Stack: text.Format(L"0x%016llX", f.StackOffset); break;
	}
	return text;
}

void CCallStackView::SwitchTo(int row) {
	// (the panes that follow the scope, this one included, hear of it from the session)
	DebugManager::Debugger().SetScopeFrame(m_Items[row].Index);
}

bool CCallStackView::OnDoubleClickList(HWND, int row, int col, POINT const& pt) {
	// (a dump too: it is always stopped)
	if (row < 0 || row >= static_cast<int>(m_Items.size()) || !DebugManager::Session().IsBroken())
		return false;

	SwitchTo(row);
	return true;
}

bool CCallStackView::OnRightClickList(HWND, int row, int, POINT const& pt) {
	auto& session = DebugManager::Session();
	const bool onRow = row >= 0 && row < static_cast<int>(m_Items.size());
	// (the rows can change while the menu is up: what it needs is taken now)
	const auto frame = onRow ? m_Items[row] : DebugEngine::StackFrameInfo{};

	enum { CmdSwitch = 1, CmdDisassembly, CmdRunTo };
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(onRow && session.IsBroken() ? MF_STRING : MF_GRAYED, CmdSwitch, L"&Switch to Frame");
	menu.AppendMenu(onRow && session.IsBroken() ? MF_STRING : MF_GRAYED, CmdDisassembly, L"Go to &Disassembly");
	// (frame 0 is where the thread is already; a dump does not run)
	menu.AppendMenu(onRow && frame.Index > 0 && session.CanRun() ? MF_STRING : MF_GRAYED, CmdRunTo, L"&Run to Frame");
	if (onRow)
		menu.SetMenuDefaultItem(CmdSwitch);
	AppendCopyItems(menu.m_hMenu);
	auto cmd = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd);
	if (HandleCopyCommand(cmd))
		return true;
	switch (cmd) {
		case CmdSwitch:
			if (DebugManager::Session().IsBroken())
				DebugManager::Debugger().SetScopeFrame(frame.Index);
			break;
		case CmdDisassembly:
			GoToDisassembly(m_hWnd, std::format(L"0x{:X}", frame.InstructionOffset).c_str());
			break;
		case CmdRunTo:
			// (until the thread returns to the frame: the instruction after its call)
			if (DebugManager::Session().CanRun()) {
				auto& dbg = DebugManager::Debugger();
				dbg.RunTo(dbg.AddressLocation(frame.InstructionOffset).c_str());
			}
			break;
	}
	return true;
}

void CCallStackView::OnTargetEnded() {
	m_Items.clear();
	m_Sources.clear();
	UpdateList(0);
	MarkDirty();
}

DebugEngine::Task<void> CCallStackView::Refresh() {
	auto alive = m_Lifetime.Watch();
	auto& dbg = DebugManager::Debugger();

	std::vector<DebugEngine::StackFrameInfo> frames;
	co_await dbg.GetStackTrace(frames);
	if (!*alive)
		co_return;

	// The line of each frame: frame 0's instruction, and for the others the call -- their instruction is the return
	// address, the one after the call, which can be on the next line.
	std::vector<ULONG64> offsets;
	for (auto& f : frames)
		offsets.push_back(f.Index == 0 ? f.InstructionOffset : f.InstructionOffset - 1);
	std::vector<DebugEngine::SourceLocation> sources;
	if (!offsets.empty())
		co_await dbg.GetSourceLocations(offsets, sources);
	if (!*alive)
		co_return;
	if (sources.size() != frames.size())
		sources.clear();

	m_Items.swap(frames);
	m_Sources.swap(sources);
	UpdateList(m_Items.size());
	RefreshDone();
}
