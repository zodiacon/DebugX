#include "stdafx.h"
#include "MemoryView.h"
#include "DebugManager.h"

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
