#include "pch.h"
#include "KernelThreadsView.h"
#include "DebugManager.h"
#include "DebugX.Kernel.h"
#include "SortHelper.h"
#include <format>

using namespace DebugEngine;

namespace {
	// KTHREAD_STATE
	PCWSTR StateName(UCHAR state) {
		static const PCWSTR names[] = { L"Initialized", L"Ready", L"Running", L"Standby", L"Terminated", L"Waiting",
			L"Transition", L"Deferred Ready", L"Gate Wait (obsolete)", L"Waiting for Process In-Swap" };
		return state < _countof(names) ? names[state] : L"?";
	}

	void CopyText(HWND hWnd, const std::wstring& text) {
		if (!::OpenClipboard(hWnd))
			return;
		::EmptyClipboard();
		auto size = (text.size() + 1) * sizeof(wchar_t);
		if (auto hData = ::GlobalAlloc(GMEM_MOVEABLE, size)) {
			memcpy(::GlobalLock(hData), text.c_str(), size);
			::GlobalUnlock(hData);
			if (!::SetClipboardData(CF_UNICODETEXT, hData))
				::GlobalFree(hData);
		}
		::CloseClipboard();
	}
}

void CKernelThreadsView::InitList() {
	m_List.InsertColumn(0, L"ETHREAD", LVCFMT_LEFT, 170);
	m_List.InsertColumn(1, L"TID", LVCFMT_RIGHT, 110);
	m_List.InsertColumn(2, L"State", LVCFMT_LEFT, 100);
	m_List.InsertColumn(3, L"Start Address", LVCFMT_LEFT, 320);
	m_List.InsertColumn(4, L"Create Time", LVCFMT_LEFT, 150);

	CImageList images;
	images.Create(16, 16, ILC_COLOR32, 2, 0);
	images.AddIcon(AtlLoadIconImage(IDI_THREADS, 0, 16, 16));
	images.AddIcon(AtlLoadIconImage(IDI_ICON9, 0, 16, 16));
	m_List.SetImageList(images, LVSIL_SMALL);
}

CString CKernelThreadsView::GetColumnText(HWND, int row, int column) const {
	auto& t = m_Items[row];
	CString text;
	switch (column) {
		case 0: text.Format(L"0x%llX", t.Address); break;
		case 1: text.Format(L"%u (0x%X)", t.Tid, t.Tid); break;
		case 2: return StateName(t.State);
		case 3:
			if (!t.StartSymbol.empty())
				return t.StartSymbol.c_str();
			text.Format(L"0x%llX", t.StartAddress);
			break;
		case 4:
			if (t.CreateTime.QuadPart)
				text = CTime(*(FILETIME*)&t.CreateTime).Format(L"%Y-%m-%d %H:%M:%S");
			break;
	}
	return text;
}

int CKernelThreadsView::GetRowImage(HWND, int row, int) const {
	return m_Items[row].Current ? 1 : 0;
}

void CKernelThreadsView::DoSort(const SortInfo* si) {
	if (si == nullptr)
		return;
	std::sort(m_Items.begin(), m_Items.end(), [&](const auto& t1, const auto& t2) {
		const bool asc = si->SortAscending;
		switch (si->SortColumn) {
			case 0: return SortHelper::Sort(t1.Address, t2.Address, asc);
			case 1: return SortHelper::Sort(t1.Tid, t2.Tid, asc);
			case 2: return SortHelper::Sort(t1.State, t2.State, asc);
			case 3: return SortHelper::Sort(t1.StartSymbol, t2.StartSymbol, asc);
			case 4: return SortHelper::Sort(t1.CreateTime.QuadPart, t2.CreateTime.QuadPart, asc);
		}
		return false;
		});
}

void CKernelThreadsView::OnTargetEnded() {
	m_Items.clear();
	UpdateList(0);
	MarkDirty();
}

bool CKernelThreadsView::CanRefresh() const {
	auto& session = DebugManager::Session();
	// (a local kernel never stops: it can always be looked at)
	return session.HasTarget() && (session.Target() & TargetType::Kernel) == TargetType::Kernel && !session.IsRunning();
}

Task<void> CKernelThreadsView::Refresh() {
	using namespace DebugX::Analysis;
	auto alive = m_Lifetime.Watch();

	// (shared with the debug thread, which can outlive the window)
	auto threads = std::make_shared<std::vector<KernelThread>>();
	co_await DebugManager::DebuggerX().RunAsync([threads](IDebugClient* client) -> HRESULT {
		CComQIPtr<IDebugSystemObjects2> system(client);
		CComQIPtr<IDebugSymbols3> symbols(client);
		ULONG64 process = 0, current = 0;
		if (!system || FAILED(system->GetImplicitProcessDataOffset(&process)) || !process)
			return E_FAIL;
		system->GetImplicitThreadDataOffset(&current);

		KernelProcessAnalyzer analyzer(client);
		analyzer.SetProcess(process);
		auto enumerator = analyzer.GetThreadEnumerator();
		// (a list that does not end -- memory that changed under a live system -- is not followed for ever)
		for (int i = 0; i < 100000; i++) {
			auto t = enumerator->GetNextThread();
			if (!t)
				break;
			KernelThread kt{ t->GetAddress(), t->GetClientId().ThreadId, t->GetState(), t->GetStartAddress() };
			kt.CreateTime = t->GetCreateTime();
			kt.Current = kt.Address == current;
			wchar_t name[512];
			ULONG64 displacement = 0;
			if (kt.StartAddress && SUCCEEDED(symbols->GetNameByOffsetWide(kt.StartAddress, name, _countof(name), nullptr, &displacement)))
				kt.StartSymbol = displacement ? std::format(L"{}+0x{:X}", name, displacement) : name;
			threads->push_back(std::move(kt));
		}
		return S_OK;
		});
	if (!*alive)
		co_return;

	m_Items = std::move(*threads);
	UpdateList(m_Items.size());
	RefreshDone();
}

bool CKernelThreadsView::OnDoubleClickList(HWND, int row, int, POINT const&) {
	if (row < 0 || row >= static_cast<int>(m_Items.size()))
		return false;
	SwitchTo(m_Items[row].Address);
	return true;
}

bool CKernelThreadsView::OnRightClickList(HWND, int row, int, POINT const& pt) {
	if (row < 0 || row >= static_cast<int>(m_Items.size()))
		return false;
	m_List.SelectItem(row);
	auto address = m_Items[row].Address;

	enum { CmdSwitch = 1, CmdDetails, CmdCopy };
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, CmdSwitch, L"&Switch to Thread");
	menu.AppendMenu(MF_STRING, CmdDetails, L"&Details (dt nt!_ETHREAD)");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, CmdCopy, L"&Copy ETHREAD");
	menu.SetMenuDefaultItem(CmdSwitch);
	switch (menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd)) {
		case CmdSwitch: SwitchTo(address); break;
		case CmdDetails: DebugManager::DebuggerX().ExecuteCommand(std::format(L"dt nt!_ETHREAD 0x{:X}", address).c_str()); break;
		case CmdCopy: CopyText(m_hWnd, std::format(L"0x{:X}", address)); break;
	}
	return true;
}

Task<void> CKernelThreadsView::SwitchTo(ULONG64 thread) {
	auto alive = m_Lifetime.Watch();
	auto hr = co_await DebugManager::DebuggerX().SetKernelThread(thread);
	if (!*alive)
		co_return;
	if (FAILED(hr)) {
		CString text;
		if (DebugManager::Session().Target() == TargetType::LocalKernel)
			text.Format(L"The registers of thread 0x%llX cannot be read now: on a local kernel they can be read only while a thread "
				L"waits, not while it runs on a processor. Try again, or see Details (dt nt!_ETHREAD).", thread);
		else
			text.Format(L"Failed to switch to thread 0x%llX (0x%08X)", thread, static_cast<unsigned>(hr));
		AtlMessageBox(m_hWnd, (PCWSTR)text, IDS_TITLE, MB_ICONERROR);
	}
	// (the list hears of it: ContextChanged)
}
