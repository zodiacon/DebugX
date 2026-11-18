#include "stdafx.h"
#include "RegistersView.h"
#include "DebugManager.h"
#include "SortHelper.h"

void CRegistersView::InitList() {
	m_List.InsertColumn(0, L"Register", LVCFMT_LEFT, 90);
	m_List.InsertColumn(1, L"Value", LVCFMT_LEFT, 320);
}

CString CRegistersView::GetColumnText(HWND hWnd, int row, int column) const {
	auto& r = m_Items[row];
	return (column == 0 ? r.Name : r.Value).c_str();
}

void CRegistersView::DoSort(const SortInfo* si) {
	if (si == nullptr)
		return;

	std::sort(m_Items.begin(), m_Items.end(), [&](const auto& r1, const auto& r2) {
		return si->SortColumn == 0 ? SortHelper::Sort(r1.Name, r2.Name, si->SortAscending) : SortHelper::Sort(r1.Value, r2.Value, si->SortAscending);
		});
}

void CRegistersView::OnTargetEnded() {
	m_Items.clear();
	m_Previous.clear();
	m_LastFrame0.clear();
	m_PreviousThread = 0;
	m_Resumed = true;
	UpdateList(0);
	MarkDirty();
}

DebugEngine::Task<void> CRegistersView::Refresh() {
	auto alive = m_Lifetime.Watch();

	std::vector<DebugEngine::RegisterInfo> registers;
	co_await DebugManager::DebuggerX().GetRegisters(registers);
	if (!*alive)
		co_return;

	// What is compared with is the state at the last stop, not at the last refresh: the same stop can be looked at more
	// than once, and the changes it made have to stay in red. Another thread has nothing in common with this one.
	// (another frame's registers are not the thread's at the stop, and are never what is compared with)
	const auto thread = DebugManager::Session().CurrentThreadId();
	const auto frame0 = DebugManager::Session().CurrentFrame() == 0;
	if (thread != m_PreviousThread) {
		m_Previous.clear();
		m_LastFrame0.clear();
		m_PreviousThread = thread;
		m_Resumed = false;
	}
	else if (m_Resumed) {
		m_Previous = m_LastFrame0;
		m_Resumed = false;
	}

	m_Items.clear();
	m_Items.reserve(registers.size());
	if (frame0)
		m_LastFrame0.clear();
	for (auto& r : registers) {
		auto it = m_Previous.find(r.Name);
		const bool changed = frame0 && it != m_Previous.end() && it->second != r.Value;
		if (frame0)
			m_LastFrame0[r.Name] = r.Value;
		m_Items.push_back({ std::move(r.Name), std::move(r.Value), changed });
	}
	UpdateList(m_Items.size());
	RefreshDone();
}

LRESULT CRegistersView::OnCustomDraw(int, LPNMHDR hdr, BOOL& handled) {
	if (hdr->hwndFrom != m_List.m_hWnd) {
		handled = FALSE;
		return 0;
	}

	auto cd = reinterpret_cast<LPNMLVCUSTOMDRAW>(hdr);
	switch (cd->nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;

		case CDDS_ITEMPREPAINT: {
			const auto row = static_cast<size_t>(cd->nmcd.dwItemSpec);
			if (row < m_Items.size() && m_Items[row].Changed) {
				cd->clrText = WTLHelper::IsDarkMode() ? RGB(255, 110, 110) : RGB(200, 0, 0);
				return CDRF_NEWFONT;
			}
			break;
		}
	}
	return CDRF_DODEFAULT;
}
