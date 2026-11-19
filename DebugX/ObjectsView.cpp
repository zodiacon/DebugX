#include "stdafx.h"
#include "ObjectsView.h"
#include "DebugManager.h"
#include "DebugX.Kernel.h"
#include "SortHelper.h"
#include "ClipboardHelper.h"
#include <format>

using namespace DebugEngine;

namespace {
	constexpr PCWSTR PathTag = L"|p:";
}

void CObjectsView::InitList() {
	m_List.InsertColumn(0, L"Name", LVCFMT_LEFT, 260);
	m_List.InsertColumn(1, L"Type", LVCFMT_LEFT, 140);
	m_List.InsertColumn(2, L"Address", LVCFMT_RIGHT, 150);
	m_List.InsertColumn(3, L"Link Target", LVCFMT_LEFT, 300);
}

CString CObjectsView::GetColumnText(HWND, int row, int column) const {
	if (row < HeaderRows) {
		switch (column) {
			case 0: return m_Path == L"\\" ? m_Path.c_str() : (m_Path + L"  (..)").c_str();
			case 1: return m_Error.empty() ? std::format(L"{} objects", m_Items.size()).c_str() : m_Error.c_str();
		}
		return L"";
	}
	auto& o = m_Items[row - HeaderRows];
	CString text;
	switch (column) {
		case 0: return o.Name.c_str();
		case 1: return o.Type.c_str();
		case 2: text.Format(L"0x%llX", o.Address); break;
		case 3: return o.LinkTarget.c_str();
	}
	return text;
}

void CObjectsView::DoSort(const SortInfo* si) {
	if (si == nullptr)
		return;
	std::sort(m_Items.begin(), m_Items.end(), [&](const auto& o1, const auto& o2) {
		const bool asc = si->SortAscending;
		switch (si->SortColumn) {
			case 0: return SortHelper::Sort(o1.Name, o2.Name, asc);
			case 1: return SortHelper::Sort(o1.Type, o2.Type, asc);
			case 2: return SortHelper::Sort(o1.Address, o2.Address, asc);
			case 3: return SortHelper::Sort(o1.LinkTarget, o2.LinkTarget, asc);
		}
		return false;
		});
}

void CObjectsView::GoTo(std::wstring path) {
	if (path.empty())
		path = L"\\";
	m_Path = std::move(path);
	RequestRefresh();
}

bool CObjectsView::OnDoubleClickList(HWND, int row, int, POINT const&) {
	if (row < HeaderRows) {
		// up
		if (m_Path != L"\\") {
			auto slash = m_Path.find_last_of(L'\\');
			GoTo(slash == 0 ? L"\\" : m_Path.substr(0, slash));
		}
		return true;
	}
	if (row - HeaderRows >= static_cast<int>(m_Items.size()))
		return false;
	auto& o = m_Items[row - HeaderRows];
	if (o.Type == L"Directory")
		GoTo(m_Path == L"\\" ? L"\\" + o.Name : m_Path + L"\\" + o.Name);
	else if (o.Type == L"SymbolicLink" && o.LinkTarget.starts_with(L'\\'))
		GoTo(o.LinkTarget);
	else
		DebugManager::DebuggerX().ExecuteCommand(std::format(L"!object 0x{:X}", o.Address).c_str());
	return true;
}

bool CObjectsView::OnRightClickList(HWND, int row, int, POINT const& pt) {
	if (row < HeaderRows || row - HeaderRows >= static_cast<int>(m_Items.size()))
		return false;
	m_List.SelectItem(row);
	auto o = m_Items[row - HeaderRows];

	enum { CmdObject = 1, CmdHeader, CmdCopyAddress, CmdCopyName };
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, CmdObject, L"&Details (!object)");
	menu.AppendMenu(MF_STRING, CmdHeader, L"&Header (dt nt!_OBJECT_HEADER)");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, CmdCopyAddress, L"Copy &Address");
	menu.AppendMenu(MF_STRING, CmdCopyName, L"Copy &Path");
	switch (menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd)) {
		case CmdObject: DebugManager::DebuggerX().ExecuteCommand(std::format(L"!object 0x{:X}", o.Address).c_str()); break;
		case CmdHeader:
			// (the header is right before the body)
			DebugManager::DebuggerX().ExecuteCommand(std::format(L"dt nt!_OBJECT_HEADER 0x{:X}-@@c++(#FIELD_OFFSET(nt!_OBJECT_HEADER, Body))", o.Address).c_str());
			break;
		case CmdCopyAddress: ClipboardHelper::CopyText(m_hWnd, std::format(L"0x{:X}", o.Address).c_str()); break;
		case CmdCopyName: ClipboardHelper::CopyText(m_hWnd, (m_Path == L"\\" ? L"\\" + o.Name : m_Path + L"\\" + o.Name).c_str()); break;
	}
	return true;
}

void CObjectsView::OnTargetEnded() {
	m_Items.clear();
	m_Error.clear();
	UpdateList(0);
	MarkDirty();
}

std::wstring CObjectsView::SaveState() const {
	return CListPane<CObjectsView>::SaveState() + PathTag + m_Path;
}

void CObjectsView::LoadState(PCWSTR state) {
	CListPane<CObjectsView>::LoadState(state);
	std::wstring_view text(state ? state : L"");
	if (auto pos = text.find(PathTag); pos != std::wstring_view::npos && pos + wcslen(PathTag) < text.size())
		m_Path = text.substr(pos + wcslen(PathTag));
}

bool CObjectsView::CanRefresh() const {
	auto& session = DebugManager::Session();
	return session.HasTarget() && (session.Target() & TargetType::Kernel) == TargetType::Kernel && !session.IsRunning();
}

Task<void> CObjectsView::Refresh() {
	using namespace DebugX::Analysis;
	auto alive = m_Lifetime.Watch();

	struct Result {
		std::vector<Object> Objects;
		bool Found{ false };
	};
	auto result = std::make_shared<Result>();
	co_await DebugManager::DebuggerX().RunAsync([result, path = m_Path](IDebugClient* client) -> HRESULT {
		KernelObjectAnalyzer analyzer(client);
		auto directory = analyzer.FindObject(path.c_str());
		if (!directory || analyzer.GetObjectTypeName(directory) != L"Directory")
			return S_FALSE;
		result->Found = true;
		for (auto& o : analyzer.EnumDirectory(directory))
			result->Objects.push_back({ o.Address, (PCWSTR)o.Name, (PCWSTR)o.Type, (PCWSTR)o.LinkTarget });
		return S_OK;
		});
	if (!*alive)
		co_return;

	m_Error = result->Found ? L"" : L"not a directory, or not there";
	m_Items = std::move(result->Objects);
	UpdateList(m_Items.size() + HeaderRows);
	RefreshDone();
}
