#pragma once

#include <vector>
#include <map>
#include "Debugger.h"
#include "Lifetime.h"
#include "PaneBase.h"
#include "ScintillaCtrl.h"
#include "AppFonts.h"
#include "resource.h"

// A window of instructions around the current thread's instruction pointer (or wherever "Go to
// Address" last sent it): a read-only CScintillaCtrl with an "asm" Lexilla lexer, a breakpoint glyph
// (click the margin, or right-click for the menu) and an arrow on the current instruction. Refreshes,
// like the other live panes, whenever the target breaks (CPaneBase); "Run to Cursor" is on the
// right-click menu. Go to Address is the address bar above the code (an address, a symbol, any
// expression the engine evaluates; Enter or Go -- empty: back to the current instruction), which
// Ctrl+G and the right-click menu move to.
// With Show Source Lines (right-click; on by default, kept with the pane), each source line the instructions come from
// is shown before them, as a comment.
class CDisassemblyView :
	public CFrameWindowImpl<CDisassemblyView, CWindow, CControlWinTraits>,
	public CPaneBase<CDisassemblyView> {
public:
	using BaseFrame = CFrameWindowImpl<CDisassemblyView, CWindow, CControlWinTraits>;

	CDisassemblyView() : m_Text(this, 2), m_AddressEdit(this, 3) {
	}

	enum { TextId = 150, AddressId, GoId };
	enum { MarkerBreakpoint, MarkerCurrent, MarkerFrame };
	enum { MarginSymbols };

	BEGIN_MSG_MAP(CDisassemblyView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WTLHelper::ThemeChangedMessage, OnThemeChanged)
		MESSAGE_HANDLER(AppFonts::FontsChangedMessage, OnFontsChanged)
		NOTIFY_HANDLER(TextId, SCN_MARGINCLICK, OnMarginClick)
		// (F9, Ctrl+F10 and Ctrl+Shift+F10 come from the frame, for the line the caret is on)
		COMMAND_ID_HANDLER(ID_DISASM_TOGGLEBREAKPOINT, OnCaretCommand)
		COMMAND_ID_HANDLER(ID_DEBUG_RUNTOCURSOR, OnCaretCommand)
		COMMAND_ID_HANDLER(ID_DEBUG_SETNEXTSTATEMENT, OnCaretCommand)
		COMMAND_ID_HANDLER(ID_EDIT_GOTO, OnGoTo)
		COMMAND_HANDLER(GoId, BN_CLICKED, OnGoClicked)
		CHAIN_MSG_MAP(CPaneBase<CDisassemblyView>)
		CHAIN_MSG_MAP(BaseFrame)
	ALT_MSG_MAP(2)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnTextContextMenu)
	ALT_MSG_MAP(3)
		MESSAGE_HANDLER(WM_CHAR, OnAddressChar)
	END_MSG_MAP()

	HWND Create(HWND hParent) override {
		return BaseFrame::Create(hParent, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
	}
	void OnFinalMessage(HWND) override {
		delete this;
	}

	// ISessionObserver
	// (a stop, a step, another thread or frame: back to where the code is running)
	void OnContextChanged() override {
		ClearGoTo();
		RequestRefresh();
	}
	void OnScopeChanged() override {
		ClearGoTo();
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

	// IPane: Show Source Lines
	std::wstring SaveState() const override {
		return m_ShowSource ? L"s=1" : L"s=0";
	}
	void LoadState(PCWSTR state) override {
		m_ShowSource = !state || wcscmp(state, L"s=0") != 0;
	}

	// CPaneBase
	DebugEngine::Task<void> Refresh();

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM lParam, BOOL&);
	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnThemeChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnFontsChanged(UINT, WPARAM wParam, LPARAM, BOOL&);
	LRESULT OnMarginClick(int, LPNMHDR, BOOL&);
	LRESULT OnTextContextMenu(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCaretCommand(WORD, WORD id, HWND, BOOL&);
	LRESULT OnGoTo(WORD, WORD, HWND, BOOL&);
	LRESULT OnGoClicked(WORD, WORD, HWND, BOOL&);
	LRESULT OnAddressChar(UINT, WPARAM, LPARAM, BOOL&);
	// Ctrl+G, the menu: to the address bar, its text selected
	void FocusAddressBar();
	// the address bar's expression, evaluated; empty: back to where the code runs
	DebugEngine::Task<void> GoToAddress();
	// back to where the code runs (a stop, a step, another thread or frame): the address bar is emptied
	void ClearGoTo();

	void ApplyColors();
	// a command of the menu (ID_DISASM_TOGGLEBREAKPOINT, ID_DEBUG_RUNTOCURSOR, ID_DEBUG_SETNEXTSTATEMENT) on an instruction
	void RunCommand(UINT id, ULONG64 address);
	DebugEngine::Task<void> ToggleBreakpointAt(ULONG64 address);
	DebugEngine::Task<void> RunToCursor(ULONG64 address);
	// the address the line under 'pos' (a buffer position) is for, or 0 if it is out of range
	ULONG64 AddressAtPosition(Scintilla::Position pos);

	static constexpr ULONG LineCount = 200;
	// the text of a source file, a line each (read once; empty if it cannot be)
	const std::vector<std::wstring>& SourceLines(const std::wstring& path);

	CContainedWindowT<CScintillaCtrl> m_Text;
	CContainedWindowT<CEdit> m_AddressEdit;
	CButton m_Go;
	std::vector<ULONG64> m_LineAddresses;		// parallel to the document's lines
	std::vector<DebugEngine::BreakpointInfo> m_Breakpoints;
	ULONG64 m_GoToAddress{ 0 };		// where Go to Address sent the view; 0: where the scope frame is
	ULONG64 m_ShownGoTo{ 0 };		// the m_GoToAddress the text is for
	bool m_ShowSource{ true };
	std::map<std::wstring, std::vector<std::wstring>> m_SourceFiles;
	Lifetime m_Lifetime;
};
