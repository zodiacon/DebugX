#include "stdafx.h"
#include "ModulesView.h"
#include "DebugManager.h"
#include "SortHelper.h"

namespace {
	// what the debugger knows about the symbols of a module (DEBUG_SYMTYPE_*)
	PCWSTR SymbolTypeName(ULONG type) {
		switch (type) {
			case DEBUG_SYMTYPE_NONE: return L"None";
			case DEBUG_SYMTYPE_COFF: return L"COFF";
			case DEBUG_SYMTYPE_CODEVIEW: return L"CodeView";
			case DEBUG_SYMTYPE_PDB: return L"PDB";
			case DEBUG_SYMTYPE_EXPORT: return L"Exports";
			case DEBUG_SYMTYPE_DEFERRED: return L"Deferred";
			case DEBUG_SYMTYPE_SYM: return L"SYM";
			case DEBUG_SYMTYPE_DIA: return L"DIA";
		}
		return L"?";
	}
}

void CModulesView::InitList() {
	m_List.InsertColumn(0, L"Name", LVCFMT_LEFT, 130);
	m_List.InsertColumn(1, L"Base", LVCFMT_RIGHT, 140);
	m_List.InsertColumn(2, L"Size", LVCFMT_RIGHT, 90);
	m_List.InsertColumn(3, L"Time Stamp", LVCFMT_LEFT, 140);
	m_List.InsertColumn(4, L"Symbols", LVCFMT_LEFT, 80);
	m_List.InsertColumn(5, L"Image Path", LVCFMT_LEFT, 300);
	m_List.InsertColumn(6, L"Symbol File", LVCFMT_LEFT, 300);

	CImageList images;
	images.Create(16, 16, ILC_COLOR32, 1, 0);
	images.AddIcon(AtlLoadIconImage(IDI_DLL, 0, 16, 16));
	m_List.SetImageList(images, LVSIL_SMALL);
}

CString CModulesView::GetColumnText(HWND hWnd, int row, int column) const {
	auto& m = m_Items[row];
	CString text;

	switch (column) {
		case 0: return m.Name.c_str();
		case 1: text.Format(L"0x%016llX", m.BaseAddress); break;
		case 2: text.Format(L"0x%X", m.Size); break;
		case 3:
			if (m.TimeStamp != 0 && m.TimeStamp != 0xffffffff)
				text = CTime(static_cast<__time64_t>(m.TimeStamp)).Format(L"%Y-%m-%d %H:%M:%S");
			break;
		case 4: return SymbolTypeName(m.Symbols);
		case 5: return m.ImageName.c_str();
		case 6: return m.SymbolFile.c_str();
	}
	return text;
}

void CModulesView::DoSort(const SortInfo* si) {
	if (si == nullptr)
		return;

	std::sort(m_Items.begin(), m_Items.end(), [&](const auto& m1, const auto& m2) {
		const bool asc = si->SortAscending;
		switch (si->SortColumn) {
			case 0: return SortHelper::Sort(m1.Name, m2.Name, asc);
			case 1: return SortHelper::Sort(m1.BaseAddress, m2.BaseAddress, asc);
			case 2: return SortHelper::Sort(m1.Size, m2.Size, asc);
			case 3: return SortHelper::Sort(m1.TimeStamp, m2.TimeStamp, asc);
			case 4: return SortHelper::Sort(m1.Symbols, m2.Symbols, asc);
			case 5: return SortHelper::Sort(m1.ImageName, m2.ImageName, asc);
			case 6: return SortHelper::Sort(m1.SymbolFile, m2.SymbolFile, asc);
		}
		return false;
		});
}

void CModulesView::OnTargetEnded() {
	m_Items.clear();
	UpdateList(0);
	MarkDirty();
}

DebugEngine::Task<void> CModulesView::Refresh() {
	auto alive = m_Lifetime.Watch();

	std::vector<DebugEngine::ModuleInfo> modules;
	co_await DebugManager::DebuggerX().EnumModules(modules);
	if (!*alive)
		co_return;

	m_Items.swap(modules);
	UpdateList(m_Items.size());
	RefreshDone();
}
