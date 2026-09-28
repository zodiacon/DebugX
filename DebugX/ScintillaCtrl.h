#pragma once

#include <string>
#include <string_view>
#include <scintilla/ScintillaTypes.h>
#include <scintilla/ScintillaMessages.h>
#include <scintilla/ScintillaStructures.h>
#include <scintilla/ScintillaCall.h>
// the classic SCI_*/SCN_* numeric macros (Scintilla::Message/Notification give the same values as
// scoped enums; the macros are what NOTIFY_HANDLER/message maps read more naturally)
#include <scintilla/Scintilla.h>

//
// A thin WTL wrapper around a Scintilla edit window. Scintilla is not an ATL control: it is a plain
// Win32 class ("Scintilla") registered once for the process by RegisterScintillaClasses (call it from
// _tWinMain, next to WTLHelper::InitDarkMode; UnregisterScintillaClasses undoes it at shutdown). A
// CScintillaCtrl is otherwise used exactly like CRichEditCtrl/CListViewCtrl: as the TBase of a
// CContainedWindowT (its static GetWndClassName() is what CContainedWindowT superclasses).
//
// CWindow-ish members (Create, MoveWindow, SetFocus, Invalidate, ...) are called directly on the
// control, as usual. The Scintilla API itself is reached through operator->() (m_Text->AppendText(...)),
// which is Scintilla::ScintillaCall bound to the control's direct-call function -- no SendMessage
// round trip. Call InitCall() once, right after Create() returns a valid HWND.
//
// The control is always UTF-8 internally (SC_CP_UTF8): text in and out of the Scintilla API is
// therefore char (UTF-8), never the wchar_t the rest of the app uses. ToUtf8/FromUtf8 below convert
// at that boundary.
//
class CScintillaCtrl : public CWindow {
public:
	using CWindow::CWindow;
	using CWindow::operator=;

	static PCWSTR GetWndClassName() {
		return L"Scintilla";
	}

	// binds the direct-call function/pointer; call once, right after Create()
	void InitCall() {
		auto fn = reinterpret_cast<Scintilla::FunctionDirect>(
			::SendMessage(m_hWnd, static_cast<UINT>(Scintilla::Message::GetDirectFunction), 0, 0));
		auto ptr = static_cast<intptr_t>(
			::SendMessage(m_hWnd, static_cast<UINT>(Scintilla::Message::GetDirectPointer), 0, 0));
		m_Call.SetFnPtr(fn, ptr);
		m_Call.SetCodePage(Scintilla::CpUtf8);
	}

	Scintilla::ScintillaCall* operator->() {
		return &m_Call;
	}
	const Scintilla::ScintillaCall* operator->() const {
		return &m_Call;
	}
	Scintilla::ScintillaCall& Call() {
		return m_Call;
	}

private:
	Scintilla::ScintillaCall m_Call;
};

// Scintilla is a plain Win32 window class, registered once for the whole process (not per-window,
// unlike an ATL CWindowImpl); call these from _tWinMain, bracketing _Module.Init()/Term() the same
// way the RichEdit DLL load/free does today.
void RegisterScintillaClasses();
void UnregisterScintillaClasses();

// the control's text is UTF-8; these convert at the boundary with the rest of the app's wide strings
std::string ToUtf8(std::wstring_view text);
std::wstring FromUtf8(std::string_view text);
