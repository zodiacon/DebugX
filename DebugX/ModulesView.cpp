#include "pch.h"
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

bool CModulesView::OnRightClickList(HWND, int row, int, POINT const& pt) {
	if (row < 0 || row >= static_cast<int>(m_Items.size()))
		return false;
	m_List.SelectItem(row);
	auto& m = m_Items[row];
	auto& session = DebugManager::Session();
	const auto attributes = ::GetFileAttributesW(m.ImageName.c_str());
	const bool onDisk = attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);

	enum { CmdLoadSymbols = 1, CmdOpenFolder, CmdCopyPath };
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(session.HasTarget() && !session.IsRunning() ? MF_STRING : (MF_STRING | MF_GRAYED), CmdLoadSymbols, L"&Load Symbols");
	menu.AppendMenu(onDisk ? MF_STRING : (MF_STRING | MF_GRAYED), CmdOpenFolder, L"&Open Containing Folder");
	menu.AppendMenu(m.ImageName.empty() ? (MF_STRING | MF_GRAYED) : MF_STRING, CmdCopyPath, L"&Copy Path");
	switch (menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd)) {
		case CmdLoadSymbols: {
			// .reload /f takes the image's name (kernel32.dll)
			auto image = m.ImageName.substr(m.ImageName.find_last_of(L"\\/") + 1);
			LoadSymbols(image.empty() ? m.Name : image);
			break;
		}
		case CmdOpenFolder: {
			// Explorer with the file selected
			auto args = L"/select,\"" + m.ImageName + L"\"";
			::ShellExecuteW(m_hWnd, nullptr, L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
			break;
		}
		case CmdCopyPath:
			if (OpenClipboard()) {
				::EmptyClipboard();
				auto size = (m.ImageName.size() + 1) * sizeof(wchar_t);
				if (auto hData = ::GlobalAlloc(GMEM_MOVEABLE, size)) {
					memcpy(::GlobalLock(hData), m.ImageName.c_str(), size);
					::GlobalUnlock(hData);
					if (!::SetClipboardData(CF_UNICODETEXT, hData))
						::GlobalFree(hData);
				}
				::CloseClipboard();
			}
			break;
	}
	return true;
}

DebugEngine::Task<void> CModulesView::LoadSymbols(std::wstring image) {
	auto alive = m_Lifetime.Watch();
	auto hr = co_await DebugManager::DebuggerX().ReloadSymbols(image.c_str());
	if (!*alive)
		co_return;
	if (FAILED(hr)) {
		CString text;
		text.Format(L"Failed to load the symbols of %s (0x%08X)", image.c_str(), static_cast<unsigned>(hr));
		AtlMessageBox(*this, (PCWSTR)text, IDS_TITLE, MB_ICONERROR);
	}
	// (the Symbols column: deferred no more)
	RequestRefresh();
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
