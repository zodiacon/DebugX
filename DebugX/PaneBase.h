#pragma once

#include <string>
#include "DebugManager.h"
#include "AppFonts.h"		// (the panes handle its FontsChangedMessage)

struct IPane;
// (PaneRegistry.cpp) a pane that goes away must not be asked for its state any more
void ForgetPane(IPane* pane);

//
// A tool window of the application, as the PaneRegistry knows it. A pane is an object that makes its own window
// (Create) and deletes itself when that is destroyed, and can hand over what it wants to be the same next time.
//
struct IPane {
	virtual ~IPane() {
		ForgetPane(this);
	}
	virtual HWND Create(HWND hParent) = 0;
	// Text that LoadState gets back, after Create, in the next run. Empty: nothing to keep.
	virtual std::wstring SaveState() const {
		return {};
	}
	virtual void LoadState(PCWSTR state) {}
};

//
// A pane that shows what the target is like, and follows the session. It does not update while it cannot be seen or
// while there is nothing to show: events only mark it "dirty", and the refresh happens when it is dirty, showing, and
// the session allows it (CanRefresh). Only one refresh is ever under way: the pane's Refresh (which may be a
// coroutine) has to end by calling RefreshDone, and if the pane was made dirty meanwhile it refreshes again.
//
//   class CMyPane : public CFrameWindowImpl<...>, public CPaneBase<CMyPane> {
//       CHAIN_MSG_MAP(CPaneBase<CMyPane>)      // in the message map
//       void Refresh();                        // what to do to bring the window up to date
//       bool CanRefresh() const;               // optional: the default is a target that is stopped
//       // OnCreate calls InitPane, OnDestroy ExitPane
//
template<typename T>
class CPaneBase : public IPane, public ISessionObserver {
public:
	BEGIN_MSG_MAP(CPaneBase)
		MESSAGE_HANDLER(WM_SHOWWINDOW, OnShowWindow)
		// the dock shows and sizes a pane's window in one go, and its parent may not be showing yet at the first of these
		MESSAGE_HANDLER(WM_WINDOWPOSCHANGED, OnShowWindow)
		// View > Refresh, forwarded by the frame to the active pane
		COMMAND_ID_HANDLER(ID_VIEW_REFRESH, OnViewRefresh)
	END_MSG_MAP()

	bool CanRefresh() const {
		return DebugManager::Session().IsBroken() && DebugManager::Session().HasTarget();
	}

	// by default, whenever the target stops and when there is a new one
	void OnBroke() override {
		RequestRefresh();
	}
	void OnTargetAttached() override {
		RequestRefresh();
	}
	void OnTargetEnded() override {
		MarkDirty();
	}

protected:
	void InitPane() {
		DebugManager::Session().AddObserver(this);
		TryRefresh();
	}

	void ExitPane() {
		DebugManager::Session().RemoveObserver(this);
	}

	// the window is out of date; brought up to date now if it can be
	void RequestRefresh() {
		m_Dirty = true;
		TryRefresh();
	}

	// the window is out of date; nothing is done about it until it is showing and can be
	void MarkDirty() {
		m_Dirty = true;
	}

	bool IsDirty() const {
		return m_Dirty;
	}

	void RefreshDone() {
		m_Refreshing = false;
		TryRefresh();
	}

private:
	void TryRefresh() {
		auto pT = static_cast<T*>(this);
		if (!m_Dirty || m_Refreshing || !pT->IsWindow() || !pT->IsWindowVisible() || !pT->CanRefresh())
			return;

		m_Dirty = false;
		m_Refreshing = true;
		pT->Refresh();
	}

	LRESULT OnShowWindow(UINT msg, WPARAM wParam, LPARAM, BOOL& handled) {
		if (msg == WM_WINDOWPOSCHANGED || wParam)
			TryRefresh();
		handled = FALSE;
		return 0;
	}

	LRESULT OnViewRefresh(WORD, WORD, HWND, BOOL&) {
		RequestRefresh();
		return 0;
	}

	bool m_Dirty{ true };
	bool m_Refreshing{ false };
};

//
// What panes with a list keep of it: the width of the columns (at 96 DPI, so that a different monitor does not
// upset them), as "w:170,130,70".
//
namespace ColumnState {
	inline std::wstring SaveColumnWidths(CListViewCtrl list) {
		const int dpi = ::GetDpiForWindow(list);
		std::wstring state(L"w:");
		const int count = list.GetHeader().GetItemCount();
		for (int i = 0; i < count; i++) {
			if (i > 0)
				state += L',';
			state += std::to_wstring(::MulDiv(list.GetColumnWidth(i), 96, dpi));
		}
		return state;
	}

	inline void LoadColumnWidths(CListViewCtrl list, PCWSTR state) {
		if (!state || state[0] != L'w' || state[1] != L':')
			return;

		const int dpi = ::GetDpiForWindow(list);
		const int count = list.GetHeader().GetItemCount();
		auto p = state + 2;
		for (int i = 0; i < count && *p; i++) {
			wchar_t* end;
			auto width = ::wcstol(p, &end, 10);
			if (end == p)
				break;
			if (width >= 20 && width <= 2000)
				list.SetColumnWidth(i, ::MulDiv(width, dpi, 96));
			p = *end == L',' ? end + 1 : end;
		}
	}
}
