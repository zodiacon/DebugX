#pragma once

#include <vector>
#include "Debugger.h"
#include "Lifetime.h"
#include "PaneBase.h"
#include "ScintillaCtrl.h"
#include "resource.h"

// A window of instructions around the current thread's instruction pointer (or wherever "Go to
// Address" last sent it): a read-only CScintillaCtrl with an "asm" Lexilla lexer, a breakpoint glyph
// (click the margin, or right-click for the menu) and an arrow on the current instruction. Refreshes,
// like the other live panes, whenever the target breaks (CPaneBase); "Go to Address" and "Run to
// Cursor" are on the right-click menu, there being nowhere else for them yet.
class CDisassemblyView :
	public CFrameWindowImpl<CDisassemblyView, CWindow, CControlWinTraits>,
	public CPaneBase<CDisassemblyView> {
public:
	using BaseFrame = CFrameWindowImpl<CDisassemblyView, CWindow, CControlWinTraits>;

	CDisassemblyView() : m_Text(this, 2) {
	}

	enum { TextId = 150 };
	enum { MarkerBreakpoint, MarkerCurrent, MarkerFrame };
	enum { MarginSymbols };

	BEGIN_MSG_MAP(CDisassemblyView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WTLHelper::ThemeChangedMessage, OnThemeChanged)
		NOTIFY_HANDLER(TextId, SCN_MARGINCLICK, OnMarginClick)
		// (F9, Ctrl+F10 and Ctrl+Shift+F10 come from the frame, for the line the caret is on)
		COMMAND_ID_HANDLER(ID_DISASM_TOGGLEBREAKPOINT, OnCaretCommand)
		COMMAND_ID_HANDLER(ID_DEBUG_RUNTOCURSOR, OnCaretCommand)
		COMMAND_ID_HANDLER(ID_DEBUG_SETNEXTSTATEMENT, OnCaretCommand)
		CHAIN_MSG_MAP(CPaneBase<CDisassemblyView>)
		CHAIN_MSG_MAP(BaseFrame)
	ALT_MSG_MAP(2)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnTextContextMenu)
	END_MSG_MAP()

	HWND Create(HWND hParent) override {
		return BaseFrame::Create(hParent, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
	}
	void OnFinalMessage(HWND) override {
		delete this;
	}

	// ISessionObserver
	void OnContextChanged() override {
		RequestRefresh();
	}
	void OnScopeChanged() override {
		RequestRefresh();
	}
	// (code patched in place)
	void OnMemoryChanged() override {
		RequestRefresh();
	}
	void OnBreakpointsChanged() override {
		RequestRefresh();
	}
	void OnTargetEnded() override;

	// CPaneBase
	DebugEngine::Task<void> Refresh();

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM lParam, BOOL&);
	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnThemeChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMarginClick(int, LPNMHDR, BOOL&);
	LRESULT OnTextContextMenu(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCaretCommand(WORD, WORD id, HWND, BOOL&);

	void ApplyColors();
	// a command of the menu (ID_DISASM_TOGGLEBREAKPOINT, ID_DEBUG_RUNTOCURSOR, ID_DEBUG_SETNEXTSTATEMENT) on an instruction
	void RunCommand(UINT id, ULONG64 address);
	DebugEngine::Task<void> ToggleBreakpointAt(ULONG64 address);
	DebugEngine::Task<void> RunToCursor(ULONG64 address);
	// the address the line under 'pos' (a buffer position) is for, or 0 if it is out of range
	ULONG64 AddressAtPosition(Scintilla::Position pos);

	static constexpr ULONG LineCount = 200;

	CContainedWindowT<CScintillaCtrl> m_Text;
	std::vector<ULONG64> m_LineAddresses;		// parallel to the document's lines
	std::vector<DebugEngine::BreakpointInfo> m_Breakpoints;
	Lifetime m_Lifetime;
};
