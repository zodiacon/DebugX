#pragma once

#include <string>

namespace Scintilla {
	class ScintillaCall;
}

//
// The two fonts the user chooses (Options > Code Font, Options > UI Font), kept in AppSettings::CodeFont/UiFont:
//   Code -- monospace text: the Command view's output and command line, Disassembly, source files, Memory, Output, Analysis;
//   UI   -- DebugX's own controls: the panes' lists, the address bars and their buttons.
// The dock's captions and tabs, menus, the status bar and dialogs keep the system's font (the dock's captions and tabs
// have fixed heights).
//
// A window that uses one takes it from Font() when it is created, and again on FontsChangedMessage (sent to every
// window of the thread's frame, floating panes too, after a change; wParam: the Kind that changed) -- a font handle
// from Font() is valid until that message has been handled, then the old one is deleted.
//
namespace AppFonts {
	enum class Kind {
		Code,
		Ui,
	};

	struct FontSpec {
		std::wstring Face;
		int PointSize10{ 90 };		// tenths of a point
		int Weight{ FW_NORMAL };
		bool Italic{ false };

		bool operator==(const FontSpec&) const = default;
	};

	inline const UINT FontsChangedMessage = ::RegisterWindowMessage(L"DebugX.FontsChanged");

	// from AppSettings, before any window uses one
	void Load();

	const FontSpec& Spec(Kind kind);
	HFONT Font(Kind kind);
	inline HFONT CodeFont() {
		return Font(Kind::Code);
	}
	inline HFONT UiFont() {
		return Font(Kind::Ui);
	}

	// the font dialog for one of them (the code font: fixed-pitch fonts only); true if it changed -- it is then saved,
	// and FontsChangedMessage sent to 'frame', its descendants and the windows it owns
	bool Choose(Kind kind, HWND frame);

	// the code font, to a Scintilla's default style; the caller then does StyleClearAll (and sets its colors again)
	void ApplyCodeFont(Scintilla::ScintillaCall& sci);

	// the height of a line of text in the font, in pixels
	int TextHeight(HFONT font);
	// the width of the text in the font, in pixels
	int TextWidth(HFONT font, PCWSTR text);
}
