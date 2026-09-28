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
	m_Hex.SetFont(L"Consolas", 90);

	InitPane();
	return 0;
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
	m_Buffer.reset();
	m_Hex.SetBufferManager(nullptr);
	m_Hex.SetSize(0);
	MarkDirty();
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

	m_Buffer = std::make_unique<MemoryBuffer>(data.data(), static_cast<uint32_t>(data.size()));
	m_Hex.SetBufferManager(m_Buffer.get());
	m_Hex.SetSize(static_cast<int64_t>(data.size()));
	m_Hex.SetBiasOffset(static_cast<int64_t>(address));
	m_Hex.GotoOffset(0);
	m_Hex.Refresh();
	RefreshDone();
}
