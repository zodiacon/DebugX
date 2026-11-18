#pragma once

#include <map>
#include "ListPane.h"
#include "resource.h"

// The registers of the current thread, as of the scope frame. The ones that are not what they were when the target last
// stopped are in red (in frame 0 only: in another one, there is nothing to compare with).
class CRegistersView : public CListPane<CRegistersView> {
public:
	BEGIN_MSG_MAP(CRegistersView)
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)
		CHAIN_MSG_MAP(CListPane<CRegistersView>)
	END_MSG_MAP()

	void InitList();
	CString GetColumnText(HWND hWnd, int row, int column) const;
	int GetRowImage(HWND, int row, int col) const {
		return -1;
	}
	void DoSort(const SortInfo* si);

	// ISessionObserver
	void OnContextChanged() override {
		RequestRefresh();
	}
	void OnScopeChanged() override {
		RequestRefresh();
	}
	void OnResumed() override {
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

	std::vector<Register> m_Items;
	// the values when the target stopped last, and whose they are: what is compared with
	std::map<std::wstring, std::wstring> m_Previous;
	// the values last seen in frame 0: what becomes m_Previous when the target runs again
	std::map<std::wstring, std::wstring> m_LastFrame0;
	ULONG m_PreviousThread{ 0 };
	bool m_Resumed{ true };
};
