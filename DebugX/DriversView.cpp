#include "pch.h"
#include "DriversView.h"
#include "DebugManager.h"
#include "DebugX.Kernel.h"
#include "SortHelper.h"
#include "ClipboardHelper.h"
#include <format>

using namespace DebugEngine;

void CDriversView::InitList() {
	m_List.InsertColumn(0, L"Name", LVCFMT_LEFT, 220);
	m_List.InsertColumn(1, L"DRIVER_OBJECT", LVCFMT_RIGHT, 150);
	m_List.InsertColumn(2, L"Image", LVCFMT_LEFT, 140);
	m_List.InsertColumn(3, L"Start", LVCFMT_RIGHT, 150);
	m_List.InsertColumn(4, L"Size", LVCFMT_RIGHT, 80);
	m_List.InsertColumn(5, L"Devices", LVCFMT_RIGHT, 60);
	m_List.InsertColumn(6, L"Unload", LVCFMT_LEFT, 240);

	CImageList images;
	images.Create(16, 16, ILC_COLOR32, 1, 0);
	images.AddIcon(AtlLoadIconImage(IDI_DLL, 0, 16, 16));
	m_List.SetImageList(images, LVSIL_SMALL);
}

CString CDriversView::GetColumnText(HWND, int row, int column) const {
	auto& d = m_Items[row];
	CString text;
	switch (column) {
		case 0: return d.Name.c_str();
		case 1: text.Format(L"0x%llX", d.Address); break;
		case 2: return d.Image.c_str();
		case 3: if (d.Start) text.Format(L"0x%llX", d.Start); break;
		case 4: if (d.Size) text.Format(L"0x%X", d.Size); break;
		case 5: text.Format(L"%u", d.Devices); break;
		case 6: return d.Unload.c_str();
	}
	return text;
}

void CDriversView::DoSort(const SortInfo* si) {
	if (si == nullptr)
		return;
	std::sort(m_Items.begin(), m_Items.end(), [&](const auto& d1, const auto& d2) {
		const bool asc = si->SortAscending;
		switch (si->SortColumn) {
			case 0: return SortHelper::Sort(d1.Name, d2.Name, asc);
			case 1: return SortHelper::Sort(d1.Address, d2.Address, asc);
			case 2: return SortHelper::Sort(d1.Image, d2.Image, asc);
			case 3: return SortHelper::Sort(d1.Start, d2.Start, asc);
			case 4: return SortHelper::Sort(d1.Size, d2.Size, asc);
			case 5: return SortHelper::Sort(d1.Devices, d2.Devices, asc);
			case 6: return SortHelper::Sort(d1.Unload, d2.Unload, asc);
		}
		return false;
		});
}

void CDriversView::OnTargetEnded() {
	m_Items.clear();
	UpdateList(0);
	MarkDirty();
}

bool CDriversView::CanRefresh() const {
	auto& session = DebugManager::Session();
	return session.HasTarget() && (session.Target() & TargetType::Kernel) == TargetType::Kernel && !session.IsRunning();
}

Task<void> CDriversView::Refresh() {
	using namespace DebugX::Analysis;
	auto alive = m_Lifetime.Watch();

	// (shared with the debug thread, which can outlive the window)
	auto drivers = std::make_shared<std::vector<Driver>>();
	co_await DebugManager::RunAsync([drivers](IDebugClient* client) -> HRESULT {
		CComQIPtr<IDebugSymbols3> symbols(client);
		KernelObjectAnalyzer analyzer(client);
		wchar_t name[512];
		for (auto& kd : analyzer.EnumDrivers()) {
			Driver d{ kd.Address, (PCWSTR)kd.Name, {}, kd.Start, kd.Size, kd.DeviceCount };
			ULONG index;
			ULONG64 base;
			if (kd.Start && SUCCEEDED(symbols->GetModuleByOffset(kd.Start, 0, &index, &base))
				&& SUCCEEDED(symbols->GetModuleNameStringWide(DEBUG_MODNAME_IMAGE, index, base, name, _countof(name), nullptr))) {
				d.Image = name;
				d.Image = d.Image.substr(d.Image.find_last_of(L"\\/") + 1);
			}
			ULONG64 displacement = 0;
			if (kd.Unload && SUCCEEDED(symbols->GetNameByOffsetWide(kd.Unload, name, _countof(name), nullptr, &displacement)))
				d.Unload = displacement ? std::format(L"{}+0x{:X}", name, displacement) : name;
			else if (kd.Unload)
				d.Unload = std::format(L"0x{:X}", kd.Unload);
			drivers->push_back(std::move(d));
		}
		return S_OK;
		});
	if (!*alive)
		co_return;

	m_Items = std::move(*drivers);
	ApplyFilter(m_Items);
	UpdateList(m_Items.size());
	RefreshDone();
}

bool CDriversView::OnDoubleClickList(HWND, int row, int, POINT const&) {
	if (row < 0 || row >= static_cast<int>(m_Items.size()))
		return false;
	DebugManager::Debugger().ExecuteCommand(std::format(L"dt nt!_DRIVER_OBJECT 0x{:X}", m_Items[row].Address).c_str());
	return true;
}

bool CDriversView::OnRightClickList(HWND, int row, int, POINT const& pt) {
	if (row < 0 || row >= static_cast<int>(m_Items.size()))
		return false;
	m_List.SelectItem(row);
	auto address = m_Items[row].Address;

	enum { CmdDetails = 1, CmdDispatch, CmdDevices, CmdCopy };
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, CmdDetails, L"&Details (dt nt!_DRIVER_OBJECT)");
	menu.AppendMenu(MF_STRING, CmdDispatch, L"Dis&patch Routines (!drvobj)");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, CmdCopy, L"&Copy DRIVER_OBJECT");
	menu.SetMenuDefaultItem(CmdDetails);
	AppendCopyItems(menu.m_hMenu);
	auto cmd = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd);
	if (HandleCopyCommand(cmd))
		return true;
	switch (cmd) {
		case CmdDetails: DebugManager::Debugger().ExecuteCommand(std::format(L"dt nt!_DRIVER_OBJECT 0x{:X}", address).c_str()); break;
		case CmdDispatch: DebugManager::Debugger().ExecuteCommand(std::format(L"!drvobj 0x{:X} 7", address).c_str()); break;
		case CmdCopy: ClipboardHelper::CopyText(m_hWnd, std::format(L"0x{:X}", address).c_str()); break;
	}
	return true;
}
