#include "stdafx.h"
#include "MemoryView.h"
#include "DebugManager.h"
#include <format>

using namespace DebugEngine;

LRESULT CMemoryView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	CFontHandle font;
	font.CreatePointFont(90, L"Segoe UI");

	m_AddressEdit.Create(*this, nullptr, nullptr, WS_CHILD | WS_VISIBLE | WS_BORDER, 0, AddressId);
	m_AddressEdit.SetFont(font);
	m_Expression = L"@$csp";		// architecture-neutral "current stack pointer"; shown until "Go" changes it
	m_AddressEdit.SetWindowText(m_Expression);
	m_Go.Create(*this, nullptr, L"Go", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_FLAT | WS_BORDER, 0, GoId);
	m_Go.SetFont(font);

	m_Hex.Create(*this, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_TABSTOP);
	m_Hex.SetReadOnly(true);
	m_Hex.SetInsertMode(false);
	m_Hex.SetFont(L"Consolas", 90);
	::SetWindowSubclass(m_Hex, HexSubclassProc, 0, 0);

	InitPane();
	return 0;
}

LRESULT CALLBACK CMemoryView::HexSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
	// target memory is overwritten, never inserted into (the control's Insert/Delete would change the buffer's size)
	if (msg == WM_KEYDOWN && wParam == VK_INSERT)
		return 0;
	if (msg == WM_NCDESTROY)
		::RemoveWindowSubclass(hWnd, HexSubclassProc, 0);
	return ::DefSubclassProc(hWnd, msg, wParam, lParam);
}

LRESULT CMemoryView::OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
	ExitPane();
	handled = FALSE;
	return 0;
}

LRESULT CMemoryView::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto cx = GET_X_LPARAM(lParam), cy = GET_Y_LPARAM(lParam);
	if (cx == 0 || cy == 0)
		return 0;

	int barHeight = 24, buttonWidth = 50, labelWidth = 4;
	m_AddressEdit.MoveWindow(labelWidth, 2, cx - labelWidth - buttonWidth - 4, barHeight - 4);
	m_Go.MoveWindow(cx - buttonWidth, 2, buttonWidth, barHeight - 4);
	m_Hex.MoveWindow(0, barHeight, cx, cy - barHeight);
	return 0;
}

LRESULT CMemoryView::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
	m_AddressEdit.SetFocus();
	return 0;
}

LRESULT CMemoryView::OnGo(WORD, WORD, HWND, BOOL&) {
	Go();
	return 0;
}

LRESULT CMemoryView::OnAddressChar(UINT, WPARAM wParam, LPARAM, BOOL& handled) {
	if (wParam == VK_RETURN)
		Go();
	else
		handled = FALSE;
	return 0;
}

void CMemoryView::Go() {
	CString expr;
	m_AddressEdit.GetWindowText(expr);
	if (expr.IsEmpty())
		return;

	m_Expression = expr;
	RequestRefresh();
}

LRESULT CMemoryView::OnHexRightClick(int, LPNMHDR hdr, BOOL& handled) {
	if (hdr->hwndFrom != m_Hex.GetHwnd()) {
		handled = FALSE;
		return 0;
	}

	enum { CmdSize1 = 1, CmdSize2, CmdSize4, CmdSize8, CmdLine16, CmdLine32 };
	const auto size = m_Hex.GetDataSize();
	const auto perLine = m_Hex.GetBytesPerLine();
	CMenu menu;
	menu.CreatePopupMenu();
	auto radio = [&](UINT id, bool checked, PCWSTR text) {
		menu.AppendMenu(MF_STRING | (checked ? MF_CHECKED : 0), id, text);
	};
	radio(CmdSize1, size == 1, L"&Bytes");
	radio(CmdSize2, size == 2, L"&Words (2 bytes)");
	radio(CmdSize4, size == 4, L"&Double Words (4 bytes)");
	radio(CmdSize8, size == 8, L"&Quad Words (8 bytes)");
	menu.AppendMenu(MF_SEPARATOR);
	radio(CmdLine16, perLine == 16, L"&16 Bytes a Line");
	radio(CmdLine32, perLine == 32, L"&32 Bytes a Line");

	CPoint pt;
	::GetCursorPos(&pt);
	switch (auto cmd = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd)) {
		case CmdSize1: case CmdSize2: case CmdSize4: case CmdSize8:
			m_Hex.SetDataSize(1 << (cmd - CmdSize1));
			break;
		case CmdLine16: m_Hex.SetBytesPerLine(16); break;
		case CmdLine32: m_Hex.SetBytesPerLine(32); break;
	}
	return 0;
}

std::wstring CMemoryView::SaveState() const {
	return std::format(L"d={};l={};e={}", m_Hex.GetDataSize(), m_Hex.GetBytesPerLine(), (PCWSTR)m_Expression);
}

void CMemoryView::LoadState(PCWSTR state) {
	// d=<data size>;l=<bytes a line>;e=<expression> (last: it could have a ';' in it)
	std::wstring_view text(state ? state : L"");
	while (!text.empty()) {
		if (text.starts_with(L"e=")) {
			CString expr(std::wstring(text.substr(2)).c_str());
			if (!expr.IsEmpty()) {
				m_Expression = expr;
				m_AddressEdit.SetWindowText(m_Expression);
			}
			break;
		}
		auto end = text.find(L';');
		auto item = text.substr(0, end);
		auto value = _wtoi(std::wstring(item.substr(std::min<size_t>(2, item.size()))).c_str());
		if (item.starts_with(L"d=") && (value == 1 || value == 2 || value == 4 || value == 8))
			m_Hex.SetDataSize(value);
		else if (item.starts_with(L"l=") && (value == 16 || value == 32))
			m_Hex.SetBytesPerLine(value);
		if (end == std::wstring_view::npos)
			break;
		text.remove_prefix(end + 1);
	}
}

void CMemoryView::OnTargetEnded() {
	m_Hex.SetBufferManager(nullptr);
	m_Buffer.reset();
	m_Hex.SetSize(0);
	UpdateWritable();
	MarkDirty();
}

void CMemoryView::UpdateWritable() {
	auto& session = DebugManager::Session();
	const bool live = (session.Target() & TargetType::Live) == TargetType::Live;
	m_Hex.SetReadOnly(!(m_Buffer && m_Buffer->GetSize() > 0 && live && session.IsBroken()));
}

Task<void> CMemoryView::Write(ULONG64 address, std::vector<BYTE> data) {
	auto alive = m_Lifetime.Watch();

	// (the MemoryChanged this causes arrives before the task completes: see OnMemoryChanged)
	m_PendingWrites++;
	auto hr = co_await DebugManager::DebuggerX().WriteMemory(address, std::move(data));
	if (!*alive)
		co_return;
	m_PendingWrites--;

	if (FAILED(hr)) {
		CString text;
		text.Format(L"Failed to write to 0x%llX (0x%08X)", address, static_cast<unsigned>(hr));
		AtlMessageBox(*this, (PCWSTR)text, IDS_TITLE, MB_ICONERROR);
		// what the target really has
		RequestRefresh();
	}
}

Task<void> CMemoryView::Refresh() {
	auto alive = m_Lifetime.Watch();

	ULONG64 address = 0;
	auto hr = co_await DebugManager::DebuggerX().Evaluate((PCWSTR)m_Expression, address);
	if (!*alive)
		co_return;
	if (FAILED(hr)) {
		RefreshDone();
		co_return;
	}

	std::vector<BYTE> data;
	co_await DebugManager::DebuggerX().ReadMemory(address, data, ChunkSize);
	if (!*alive)
		co_return;

	// the same place again (another stop, or memory written): the caret stays where it was
	const bool samePlace = m_Buffer && m_Buffer->GetAddress() == address;
	const auto caret = samePlace ? m_Hex.GetCaretOffset() : 0;

	const auto size = static_cast<int64_t>(data.size());
	auto buffer = std::make_unique<TargetMemoryBuffer>(address, std::move(data), [this](ULONG64 address, std::vector<BYTE> data) {
		Write(address, std::move(data));
		});
	m_Hex.SetBufferManager(buffer.get());
	m_Buffer = std::move(buffer);
	m_Hex.SetSize(size);
	m_Hex.SetBiasOffset(static_cast<int64_t>(address));
	m_Hex.GotoOffset(caret < size ? caret : 0);
	m_Hex.Refresh();
	UpdateWritable();
	RefreshDone();
}
