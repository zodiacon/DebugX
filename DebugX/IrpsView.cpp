#include "pch.h"
#include "IrpsView.h"
#include "DebugManager.h"
#include "DebugX.Kernel.h"
#include "SortHelper.h"
#include "ClipboardHelper.h"
#include <format>

using namespace DebugEngine;

namespace {
	constexpr PCWSTR AllTag = L"|a:";
}

void CIrpsView::InitList() {
	m_List.InsertColumn(0, L"IRP", LVCFMT_RIGHT, 150);
	m_List.InsertColumn(1, L"Process", LVCFMT_LEFT, 150);
	m_List.InsertColumn(2, L"TID", LVCFMT_RIGHT, 100);
	m_List.InsertColumn(3, L"Major Function", LVCFMT_LEFT, 200);
	m_List.InsertColumn(4, L"Minor", LVCFMT_RIGHT, 50);
	m_List.InsertColumn(5, L"Driver", LVCFMT_LEFT, 180);
	m_List.InsertColumn(6, L"File", LVCFMT_LEFT, 260);
	m_List.InsertColumn(7, L"Flags", LVCFMT_RIGHT, 90);
}

CString CIrpsView::GetColumnText(HWND, int row, int column) const {
	auto& irp = m_Items[row];
	CString text;
	switch (column) {
		case 0: text.Format(L"0x%llX", irp.Address); break;
		case 1: text.Format(L"%s (%u)", irp.Process.c_str(), irp.ProcessId); break;
		case 2: text.Format(L"%u (0x%X)", irp.ThreadId, irp.ThreadId); break;
		case 3: return DebugX::Analysis::MajorFunctionName(irp.Major);
		case 4: text.Format(L"0x%X", irp.Minor); break;
		case 5: return irp.Driver.c_str();
		case 6: return irp.File.c_str();
		case 7: text.Format(L"0x%08X", irp.Flags); break;
	}
	return text;
}

void CIrpsView::DoSort(const SortInfo* si) {
	if (si == nullptr)
		return;
	std::sort(m_Items.begin(), m_Items.end(), [&](const auto& i1, const auto& i2) {
		const bool asc = si->SortAscending;
		switch (si->SortColumn) {
			case 0: return SortHelper::Sort(i1.Address, i2.Address, asc);
			case 1: return SortHelper::Sort(i1.Process, i2.Process, asc);
			case 2: return SortHelper::Sort(i1.ThreadId, i2.ThreadId, asc);
			case 3: return SortHelper::Sort(i1.Major, i2.Major, asc);
			case 4: return SortHelper::Sort(i1.Minor, i2.Minor, asc);
			case 5: return SortHelper::Sort(i1.Driver, i2.Driver, asc);
			case 6: return SortHelper::Sort(i1.File, i2.File, asc);
			case 7: return SortHelper::Sort(i1.Flags, i2.Flags, asc);
		}
		return false;
		});
}

bool CIrpsView::OnDoubleClickList(HWND, int row, int, POINT const&) {
	if (row < 0 || row >= static_cast<int>(m_Items.size()))
		return false;
	DebugManager::DebuggerX().ExecuteCommand(std::format(L"dt nt!_IRP 0x{:X}", m_Items[row].Address).c_str());
	return true;
}

bool CIrpsView::OnRightClickList(HWND, int row, int, POINT const& pt) {
	const bool valid = row >= 0 && row < static_cast<int>(m_Items.size());
	if (valid)
		m_List.SelectItem(row);
	auto address = valid ? m_Items[row].Address : 0;
	auto flags = [&](bool enabled) { return enabled ? MF_STRING : MF_STRING | MF_GRAYED; };

	enum { CmdDetails = 1, CmdIrp, CmdCopy, CmdAll };
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(flags(valid), CmdDetails, L"&Details (dt nt!_IRP)");
	menu.AppendMenu(flags(valid), CmdIrp, L"&Stack Locations (!irp)");
	menu.AppendMenu(flags(valid), CmdCopy, L"&Copy IRP");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING | (m_AllProcesses ? MF_CHECKED : 0), CmdAll, L"&All Processes");
	switch (menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd)) {
		case CmdDetails: DebugManager::DebuggerX().ExecuteCommand(std::format(L"dt nt!_IRP 0x{:X}", address).c_str()); break;
		case CmdIrp: DebugManager::DebuggerX().ExecuteCommand(std::format(L"!irp 0x{:X}", address).c_str()); break;
		case CmdCopy: ClipboardHelper::CopyText(m_hWnd, std::format(L"0x{:X}", address).c_str()); break;
		case CmdAll:
			m_AllProcesses = !m_AllProcesses;
			RequestRefresh();
			break;
	}
	return true;
}

void CIrpsView::OnTargetEnded() {
	m_Items.clear();
	UpdateList(0);
	MarkDirty();
}

std::wstring CIrpsView::SaveState() const {
	return CListPane<CIrpsView>::SaveState() + AllTag + (m_AllProcesses ? L"1" : L"0");
}

void CIrpsView::LoadState(PCWSTR state) {
	CListPane<CIrpsView>::LoadState(state);
	std::wstring_view text(state ? state : L"");
	if (auto pos = text.find(AllTag); pos != std::wstring_view::npos)
		m_AllProcesses = text.substr(pos + wcslen(AllTag)).starts_with(L'1');
}

bool CIrpsView::CanRefresh() const {
	auto& session = DebugManager::Session();
	return session.HasTarget() && (session.Target() & TargetType::Kernel) == TargetType::Kernel && !session.IsRunning();
}

Task<void> CIrpsView::Refresh() {
	using namespace DebugX::Analysis;
	auto alive = m_Lifetime.Watch();

	// (shared with the debug thread, which can outlive the window)
	auto irps = std::make_shared<std::vector<Irp>>();
	co_await DebugManager::DebuggerX().RunAsync([irps, all = m_AllProcesses](IDebugClient* client) -> HRESULT {
		CComQIPtr<IDebugSystemObjects2> system(client);
		ULONG64 implicit = 0;
		if (!system || FAILED(system->GetImplicitProcessDataOffset(&implicit)))
			return E_FAIL;
		KernelObjectAnalyzer objects(client);
		auto add = [&](KernelProcessAnalyzer* p) {
			auto threads = p->GetThreadEnumerator();
			std::wstring image = (PCWSTR)p->GetImageName();
			const auto pid = p->GetProcessId();
			// (a list that does not end -- memory that changed under a live system -- is not followed for ever)
			for (int i = 0; i < 100000; i++) {
				auto t = threads->GetNextThread();
				if (!t)
					break;
				for (auto& irp : objects.EnumThreadIrps(t->GetAddress()))
					irps->push_back({ irp.Address, pid, t->GetClientId().ThreadId, image, irp.MajorFunction, irp.MinorFunction,
						(PCWSTR)irp.DriverName, (PCWSTR)irp.FileName, irp.Flags });
			}
		};
		if (all) {
			KernelProcessEnumerator processes(client);
			for (int i = 0; i < 100000; i++) {
				auto p = processes.GetNextProcess();
				if (!p)
					break;
				add(p);
			}
		}
		else {
			KernelProcessAnalyzer process(client);
			process.SetProcess(implicit);
			add(&process);
		}
		return S_OK;
		});
	if (!*alive)
		co_return;

	m_Items = std::move(*irps);
	UpdateList(m_Items.size());
	RefreshDone();
}
