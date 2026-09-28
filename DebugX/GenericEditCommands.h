#pragma once

#include "ScintillaCtrl.h"

template<typename T>
class CGenericEditCommands {
public:
	BEGIN_MSG_MAP(CGenericEditCommands<T>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_CLEAR, OnEditClear)
		COMMAND_ID_HANDLER(ID_EDIT_CLEAR_ALL, OnEditClearAll)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnEditCopy)
		COMMAND_ID_HANDLER(ID_EDIT_CUT, OnEditCut)
		COMMAND_ID_HANDLER(ID_EDIT_PASTE, OnEditPaste)
		COMMAND_ID_HANDLER(ID_EDIT_SELECT_ALL, OnEditSelectAll)
		COMMAND_ID_HANDLER(ID_EDIT_UNDO, OnEditUndo)
	END_MSG_MAP()

	LRESULT OnEditClear(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
		::SendMessage(::GetFocus(), WM_CLEAR, 0, 0);
		return 0;
	}

	LRESULT OnEditCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
		::SendMessage(::GetFocus(), WM_COPY, 0, 0);
		return 0;
	}

	LRESULT OnEditCut(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
		::SendMessage(::GetFocus(), WM_CUT, 0, 0);
		return 0;
	}

	LRESULT OnEditPaste(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
		::SendMessage(::GetFocus(), WM_PASTE, 0, 0);
		return 0;
	}

	LRESULT OnEditUndo(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
		::SendMessage(::GetFocus(), WM_UNDO, 0, 0);
		return 0;
	}

	LRESULT OnEditClearAll(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
		auto focus = ::GetFocus();
		if (IsScintilla(focus))
			::SendMessage(focus, SCI_CLEARALL, 0, 0);
		else if (CEdit edit(focus); edit) {
			edit.SetSel(0, -1);
			edit.Clear();
		}
		return 0;
	}

	LRESULT OnEditSelectAll(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
		auto focus = ::GetFocus();
		if (IsScintilla(focus))
			::SendMessage(focus, SCI_SETSEL, 0, -1);
		else if (CEdit edit(focus); edit)
			edit.SetSel(0, -1);
		return 0;
	}

private:
	// a Scintilla window answers SCI_SETSEL/SCI_CLEARALL by message number, but not the CEdit/RichEdit
	// EM_SETSEL semantics the two handlers above otherwise assume; the window's class name is
	// "ATL:Scintilla" (any CScintillaCtrl -- the Command view's output today, source/disassembly views
	// later -- is ATL-superclassed the same way CRichEditCtrl is), never just "Scintilla"
	static bool IsScintilla(HWND hWnd) {
		WCHAR cls[32];
		return ::GetClassName(hWnd, cls, _countof(cls)) != 0 && ::wcsstr(cls, L"Scintilla") != nullptr;
	}
};
