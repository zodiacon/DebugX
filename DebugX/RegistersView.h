#pragma once

#include <map>
#include "ListPane.h"
#include "resource.h"

// The registers of the current thread, as of the scope frame. The ones that are not what they were when the target last
// stopped are in red (in frame 0 only: in another one, there is nothing to compare with).
// In frame 0 of a live target that is stopped, a value can be changed in place: double-click it, or F2 / Enter; Enter
// (or moving away) sets it, Escape leaves it as it was.
class CRegistersView : public CListPane<CRegistersView> {
public:
	CRegistersView() : m_Editor(this, 1) {
	}

	BEGIN_MSG_MAP(CRegistersView)
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)
		NOTIFY_CODE_HANDLER(LVN_KEYDOWN, OnListKeyDown)
		NOTIFY_CODE_HANDLER(LVN_BEGINSCROLL, OnListScroll)
		CHAIN_MSG_MAP(CListPane<CRegistersView>)
	ALT_MSG_MAP(1)
		MESSAGE_HANDLER(WM_KEYDOWN, OnEditorKeyDown)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnEditorKillFocus)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnEditorGetDlgCode)
	END_MSG_MAP()

	void InitList();
	CString GetColumnText(HWND hWnd, int row, int column) const;
	int GetRowImage(HWND, int row, int col) const {
		return -1;
	}
	void DoSort(const SortInfo* si);
	bool OnDoubleClickList(HWND, int row, int col, POINT const& pt);

	// ISessionObserver
	void OnContextChanged() override {
		RequestRefresh();
	}
	void OnScopeChanged() override {
		RequestRefresh();
	}
	void OnResumed() override {
		EndEdit(false);
		m_Resumed = true;
	}
	void OnTargetEnded() override;

	// CPaneBase
	DebugEngine::Task<void> Refresh();

private:
	struct Register {
		std::wstring Name;
		std::wstring Value;
		bool Changed;
	};

	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnListKeyDown(int, LPNMHDR hdr, BOOL& handled);
	LRESULT OnListScroll(int, LPNMHDR hdr, BOOL& handled);
	LRESULT OnEditorKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& handled);
	LRESULT OnEditorKillFocus(UINT, WPARAM, LPARAM, BOOL& handled);
	LRESULT OnEditorGetDlgCode(UINT, WPARAM, LPARAM, BOOL&);

	// registers can be set: frame 0 of a live target that is stopped
	bool CanEdit() const;
	void BeginEdit(int row);
	// 'commit': the value typed is set (if it is not what was there)
	void EndEdit(bool commit);
	DebugEngine::Task<void> SetRegister(std::wstring name, std::wstring value);

	CContainedWindowT<CEdit> m_Editor;
	std::wstring m_EditName, m_EditValue;		// what is being edited, and what it was
	bool m_Editing{ false };

	std::vector<Register> m_Items;
	// the values when the target stopped last, and whose they are: what is compared with
	std::map<std::wstring, std::wstring> m_Previous;
	// the values last seen in frame 0: what becomes m_Previous when the target runs again
	std::map<std::wstring, std::wstring> m_LastFrame0;
	ULONG m_PreviousThread{ 0 };
	bool m_Resumed{ true };
};
