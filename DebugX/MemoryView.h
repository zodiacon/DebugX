#pragma once

#include <memory>
#include <atltheme.h>		// CBufferedPaintWindowImpl, which CHexControl is built on
#include <SimpleHexControl.h>
#include "TargetMemoryBuffer.h"
#include "Debugger.h"
#include "Lifetime.h"
#include "PaneBase.h"
#include "ScintillaCtrl.h"
#include "ThemedBackground.h"
#include "resource.h"

// A live view of target memory: an address/expression box ("Go", or Enter) reads a fixed-size chunk
// there into a CHexControl (WTLHelper). The expression -- not just the address it last resolved to --
// is what's kept, so a register-based one ("@$csp", say) tracks its target across each stop the way
// CanRefresh's usual "the target just broke" trigger implies it should.
// While a live target is stopped the bytes can be edited (overwrite only: the Insert key is kept from the control):
// each change is written to the target as it is made (TargetMemoryBuffer), and shows in red until the next refresh.
// Right-click: the display format, and Search / Search Next (bytes, a string; in all of the process's memory from the caret).
// The display format is hex (in the hex control: bytes, words, double or quad words, editable) or, read-only in a text view,
// signed decimal integers, floats, doubles, ASCII or UTF-16 text -- as dd, df, dD, da, du show them.
class CMemoryView :
	public CFrameWindowImpl<CMemoryView, CWindow, CControlWinTraits>,
	public CThemedBackground<CMemoryView>,
	public CPaneBase<CMemoryView> {
public:
	using BaseFrame = CFrameWindowImpl<CMemoryView, CWindow, CControlWinTraits>;

	CMemoryView() : m_AddressEdit(this, 1), m_Formatted(this, 2) {
	}

	enum { AddressId = 200, GoId };

	BEGIN_MSG_MAP(CMemoryView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WTLHelper::ThemeChangedMessage, OnThemeChanged)
		MESSAGE_HANDLER(AppFonts::FontsChangedMessage, OnFontsChanged)
		COMMAND_HANDLER(GoId, BN_CLICKED, OnGo)
		COMMAND_ID_HANDLER(ID_EDIT_GOTO, OnGoTo)
		NOTIFY_CODE_HANDLER(NM_RCLICK, OnHexRightClick)
		CHAIN_MSG_MAP(CPaneBase<CMemoryView>)
		CHAIN_MSG_MAP(CThemedBackground<CMemoryView>)
		CHAIN_MSG_MAP(BaseFrame)
	ALT_MSG_MAP(1)
		MESSAGE_HANDLER(WM_CHAR, OnAddressChar)
	ALT_MSG_MAP(2)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnFormattedContextMenu)
	END_MSG_MAP()

	HWND Create(HWND hParent) override {
		return BaseFrame::Create(hParent, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
	}
	void OnFinalMessage(HWND) override {
		delete this;
	}

	// ISessionObserver
	void OnTargetEnded() override;

	// IPane: the display format ("d=4;l=16": data size, bytes a line) and the address expression
	std::wstring SaveState() const override;
	void LoadState(PCWSTR state) override;

	void OnSessionChanged() override {
		UpdateWritable();
	}
	// (one of this pane's own writes is already showing)
	void OnMemoryChanged() override {
		if (m_PendingWrites == 0)
			RequestRefresh();
	}

	// CPaneBase
	bool CanRefresh() const {
		return !m_Expression.IsEmpty() && CPaneBase<CMemoryView>::CanRefresh();
	}
	DebugEngine::Task<void> Refresh();

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM lParam, BOOL&);
	LRESULT OnFontsChanged(UINT, WPARAM wParam, LPARAM, BOOL&);
	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGo(WORD, WORD, HWND, BOOL&);
	// the display format: bytes, words, double or quad words; 16 or 32 bytes a line
	LRESULT OnHexRightClick(int, LPNMHDR hdr, BOOL& handled);
	// Ctrl+G: the address box, its text selected to be typed over
	LRESULT OnGoTo(WORD, WORD, HWND, BOOL&) {
		m_AddressEdit.SetFocus();
		m_AddressEdit.SetSel(0, -1);
		return 0;
	}
	LRESULT OnAddressChar(UINT, WPARAM wParam, LPARAM, BOOL& handled);
	LRESULT OnFormattedContextMenu(UINT, WPARAM, LPARAM lParam, BOOL&);
	// the right-click menu, at a point on the screen
	void ShowMenu(POINT pt);

	enum class Format { Hex, Int32, Float, Double, Ascii, Unicode };
	// shows the hex control or the text view, as the format is
	void SetFormat(Format format);
	// the buffer in the text view, in the format
	void RenderFormatted();
	void ApplyFormattedColors();
	LRESULT OnThemeChanged(UINT, WPARAM, LPARAM, BOOL& handled) {
		ApplyFormattedColors();
		if (m_Format != Format::Hex)
			RenderFormatted();
		handled = FALSE;
		return 0;
	}

	void Go();
	// Search: the bytes asked for (hex bytes, "text" or L"text"), from the caret on; Search Next: from past it
	DebugEngine::Task<void> Search(bool next);
	// "4D 5A", "MZ", L"MZ" as bytes; empty if it is none of them
	static std::vector<BYTE> ParsePattern(const CString& text);
	// the bytes can be edited when there are some, of a live target that is stopped
	void UpdateWritable();
	DebugEngine::Task<void> Write(ULONG64 address, std::vector<BYTE> data);
	// keeps the Insert key (insert mode) from the hex control
	static LRESULT CALLBACK HexSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

	static constexpr ULONG ChunkSize = 4096;

	CContainedWindowT<CEdit> m_AddressEdit;
	CButton m_Go;
	CHexControl m_Hex;
	CContainedWindowT<CScintillaCtrl> m_Formatted;
	Format m_Format{ Format::Hex };
	ULONG64 m_FormattedAddress{ 0 };
	std::unique_ptr<TargetMemoryBuffer> m_Buffer;
	int m_PendingWrites{ 0 };
	CString m_Expression;		// what is loaded: re-evaluated on every refresh, so a register expression follows it
	// what Search looks for, as typed and as bytes; and how much of what it found is selected after the refresh
	CString m_SearchText;
	std::vector<BYTE> m_SearchPattern;
	int64_t m_SelectAfterRefresh{ 0 };
	Lifetime m_Lifetime;
};
