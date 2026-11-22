#pragma once

// Messages between DebugX's own windows.

// to the main frame (or, from it, to the Watch pane): add the expression lParam points to (PCWSTR, valid during the
// call) to the Watch pane, which is shown
constexpr UINT WM_ADDWATCH = WM_APP + 1;

// sends WM_ADDWATCH to the main frame -- the root owner, also of a pane floating in a window of its own
inline void AddToWatch(HWND from, PCWSTR expression) {
	if (expression && *expression)
		::SendMessage(::GetAncestor(from, GA_ROOTOWNER), WM_ADDWATCH, 0, reinterpret_cast<LPARAM>(expression));
}

// to the main frame (or, from it, to the Disassembly pane): show the code at the address expression lParam points to
// (PCWSTR, valid during the call) in the Disassembly pane, which is shown -- as its address bar's Go to does
constexpr UINT WM_GOTODISASSEMBLY = WM_APP + 2;

inline void GoToDisassembly(HWND from, PCWSTR expression) {
	if (expression && *expression)
		::SendMessage(::GetAncestor(from, GA_ROOTOWNER), WM_GOTODISASSEMBLY, 0, reinterpret_cast<LPARAM>(expression));
}
