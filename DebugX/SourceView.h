#pragma once

#include <string>
#include <vector>
#include "Debugger.h"
#include "Lifetime.h"
#include "PaneBase.h"
#include "ScintillaCtrl.h"
#include "resource.h"

//
// A source file, as a document (one per file; SourceDocuments opens them and tells them where the target is). A
// read-only CScintillaCtrl with a "cpp" Lexilla lexer for C and C++ files, line numbers, and a margin of markers:
// breakpoints (click the margin, or F9 / right-click for the menu) and arrows on the line the current thread is at and
// on the line of the scope frame chosen in the call stack. The breakpoints refresh like any live pane (CPaneBase); the
// arrows are SetLocation's.
// When the target is stopped, the mouse resting on a name shows its value (as the Watch pane would: IDebugger::EvaluateWatches).
//
class CSourceView :
	public CFrameWindowImpl<CSourceView, CWindow, CControlWinTraits>,
	public CPaneBase<CSourceView> {
public:
	using BaseFrame = CFrameWindowImpl<CSourceView, CWindow, CControlWinTraits>;

	explicit CSourceView(std::wstring path) : m_Path(std::move(path)), m_Text(this, 2) {
	}

	enum { TextId = 151 };
	enum { MarkerBreakpoint, MarkerBreakpointDisabled, MarkerCurrent, MarkerCurrentLine, MarkerFrame, MarkerFrameLine };
	enum { MarginNumbers, MarginSymbols };

	BEGIN_MSG_MAP(CSourceView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WTLHelper::ThemeChangedMessage, OnThemeChanged)
		NOTIFY_HANDLER(TextId, SCN_MARGINCLICK, OnMarginClick)
		NOTIFY_HANDLER(TextId, SCN_DWELLSTART, OnDwellStart)
		NOTIFY_HANDLER(TextId, SCN_DWELLEND, OnDwellEnd)
		// (F9 and Ctrl+F10 come from the frame, for the line the caret is on)
		COMMAND_ID_HANDLER(ID_DISASM_TOGGLEBREAKPOINT, OnToggleBreakpoint)
		COMMAND_ID_HANDLER(ID_DEBUG_RUNTOCURSOR, OnRunToCursor)
		COMMAND_ID_HANDLER(ID_DEBUG_SETNEXTSTATEMENT, OnSetNextStatement)
		COMMAND_ID_HANDLER(ID_EDIT_GOTO, OnGoTo)
		CHAIN_MSG_MAP(CPaneBase<CSourceView>)
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

	// the views of source files there are
	static const std::vector<CSourceView*>& Views() {
		return s_Views;
	}

	const std::wstring& Path() const {
		return m_Path;
	}
	// the name the symbols have for the file, if it is known: a breakpoint on one of its lines goes by it
	void SetSymbolFile(const std::wstring& file) {
		if (!file.empty())
			m_SymbolFile = file;
	}
	// The lines (1-based; 0: none) with the current thread's arrow and with the scope frame's. 'show': the caret goes to
	// the scope frame's line (or else the current one) and it is scrolled into view.
	void SetLocation(ULONG currentLine, ULONG frameLine, bool show);
	// puts the caret on the line and scrolls it into view
	void GoToLine(ULONG line);

	// ISessionObserver
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
	LRESULT OnDwellStart(int, LPNMHDR, BOOL&);
	LRESULT OnDwellEnd(int, LPNMHDR, BOOL&);
	LRESULT OnTextContextMenu(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnToggleBreakpoint(WORD, WORD, HWND, BOOL&);
	LRESULT OnRunToCursor(WORD, WORD, HWND, BOOL&);
	LRESULT OnSetNextStatement(WORD, WORD, HWND, BOOL&);
	LRESULT OnGoTo(WORD, WORD, HWND, BOOL&);

	// reads the file into the control (UTF-8, UTF-16 or the ANSI code page); false if it cannot be read
	bool LoadFile();
	void ApplyColors();
	void SetMarker(ULONG line, int marker, int lineMarker);
	ULONG CaretLine();
	// how the engine is to be told about a line of this file: `file:line`
	std::wstring LineExpression(ULONG line) const;
	DebugEngine::Task<void> ToggleBreakpointAt(ULONG line);
	DebugEngine::Task<void> RunToLine(ULONG line);
	DebugEngine::Task<void> SetNextStatement(ULONG line);
	// what Add to Watch adds: the selection (on one line), or else the word at the caret; empty if neither is one
	std::wstring WatchExpression();
	// what a value is shown for when the mouse rests at the position: the selection, if it is in it (on one line); else the
	// name there, with what it is a member of (a.b, p->b, ns::b, this->b); empty if it is not a name
	std::wstring ExpressionAt(Scintilla::Position pos);
	DebugEngine::Task<void> ShowValue(Scintilla::Position pos, std::wstring expression, ULONG dwell);

	struct LineBreakpoint {
		ULONG Id;
		ULONG Line;
		bool Enabled;
	};

	std::wstring m_Path;
	std::wstring m_SymbolFile;
	CContainedWindowT<CScintillaCtrl> m_Text;
	bool m_IsCode{ false };
	std::vector<LineBreakpoint> m_Breakpoints;
	ULONG m_CurrentLine{ 0 }, m_FrameLine{ 0 };
	// which time the mouse rested (a value that comes after the mouse moved on is not shown)
	ULONG m_Dwell{ 0 };
	Lifetime m_Lifetime;

	static inline std::vector<CSourceView*> s_Views;
};
