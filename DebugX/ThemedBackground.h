#pragma once

//
// A pane's own background in the theme's color. A CFrameWindowImpl window's class brush is (HBRUSH)(COLOR_WINDOW + 1), a
// system color index that user32 turns into the real system color inside DefWindowProc -- past WTLHelper's GetSysColor /
// GetSysColorBrush hooks -- so in dark mode the background came out white. It shows wherever a pane has gaps between
// its controls, and around themed buttons, which paint their corners with the parent's background
// (DrawThemeParentBackground sends the parent WM_ERASEBKGND). A pane that hosts buttons or leaves gaps chains this:
//
//   class CMyPane : public CFrameWindowImpl<...>, public CThemedBackground<CMyPane> {
//       CHAIN_MSG_MAP(CThemedBackground<CMyPane>)     // in the message map, before the frame's
//
template<typename T>
class CThemedBackground {
public:
	BEGIN_MSG_MAP(CThemedBackground)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
	END_MSG_MAP()

private:
	LRESULT OnEraseBackground(UINT, WPARAM wParam, LPARAM, BOOL&) {
		CDCHandle dc(reinterpret_cast<HDC>(wParam));
		CRect rc;
		static_cast<T*>(this)->GetClientRect(&rc);
		// (GetSysColor is the hooked one: the dark color in dark mode)
		dc.FillSolidRect(&rc, ::GetSysColor(COLOR_WINDOW));
		return 1;
	}
};
