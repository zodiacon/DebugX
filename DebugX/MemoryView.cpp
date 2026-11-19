#include "stdafx.h"
#include "MemoryView.h"
#include "DebugManager.h"
#include <format>
#include "InputDlg.h"
#include "ScintillaCtrl.h"

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

	// (the other formats: text)
	m_Formatted.Create(*this, nullptr, nullptr, WS_CHILD | WS_VSCROLL | WS_HSCROLL | WS_CLIPSIBLINGS);
	m_Formatted.InitCall();
	m_Formatted->SetUndoCollection(false);
	m_Formatted->SetWrapMode(Scintilla::Wrap::None);
	m_Formatted->SetMarginWidthN(1, 0);
	m_Formatted->StyleSetFont(static_cast<int>(Scintilla::StylesCommon::Default), "Consolas");
	m_Formatted->StyleSetSizeFractional(static_cast<int>(Scintilla::StylesCommon::Default), 900);
	ApplyFormattedColors();

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
	m_Formatted.MoveWindow(0, barHeight, cx, cy - barHeight);
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

	CPoint pt;
	::GetCursorPos(&pt);
	ShowMenu(pt);
	return 0;
}

LRESULT CMemoryView::OnFormattedContextMenu(UINT, WPARAM, LPARAM lParam, BOOL&) {
	CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	if (pt.x == -1) {
		pt = { 0, 0 };
		m_Formatted.ClientToScreen(&pt);
	}
	ShowMenu(pt);
	return 0;
}

void CMemoryView::ShowMenu(POINT pt) {
	enum { CmdSize1 = 1, CmdSize2, CmdSize4, CmdSize8, CmdLine16, CmdLine32, CmdSearch, CmdSearchNext,
		CmdInt32, CmdFloat, CmdDouble, CmdAscii, CmdUnicode };
	const bool hex = m_Format == Format::Hex;
	const auto size = m_Hex.GetDataSize();
	const auto perLine = m_Hex.GetBytesPerLine();
	CMenu menu;
	menu.CreatePopupMenu();
	auto radio = [&](UINT id, bool checked, PCWSTR text) {
		menu.AppendMenu(MF_STRING | (checked ? MF_CHECKED : 0), id, text);
	};
	radio(CmdSize1, hex && size == 1, L"&Bytes");
	radio(CmdSize2, hex && size == 2, L"&Words (2 bytes)");
	radio(CmdSize4, hex && size == 4, L"&Double Words (4 bytes)");
	radio(CmdSize8, hex && size == 8, L"&Quad Words (8 bytes)");
	radio(CmdInt32, m_Format == Format::Int32, L"Signed Decimal (&4 bytes)");
	radio(CmdFloat, m_Format == Format::Float, L"&Float (4 bytes)");
	radio(CmdDouble, m_Format == Format::Double, L"D&ouble (8 bytes)");
	radio(CmdAscii, m_Format == Format::Ascii, L"&ASCII Text");
	radio(CmdUnicode, m_Format == Format::Unicode, L"&Unicode Text (UTF-16)");
	menu.AppendMenu(MF_SEPARATOR);
	radio(CmdLine16, perLine == 16, L"&16 Bytes a Line");
	radio(CmdLine32, perLine == 32, L"&32 Bytes a Line");
	menu.AppendMenu(MF_SEPARATOR);
	const auto canSearch = m_Buffer && DebugManager::Session().HasTarget() && !DebugManager::Session().IsRunning();
	menu.AppendMenu(canSearch ? MF_STRING : MF_STRING | MF_GRAYED, CmdSearch, L"&Search...");
	menu.AppendMenu(canSearch && !m_SearchPattern.empty() ? MF_STRING : MF_STRING | MF_GRAYED, CmdSearchNext, L"Search &Next");

	switch (auto cmd = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd)) {
		case CmdSize1: case CmdSize2: case CmdSize4: case CmdSize8:
			m_Hex.SetDataSize(1 << (cmd - CmdSize1));
			SetFormat(Format::Hex);
			break;
		case CmdLine16: m_Hex.SetBytesPerLine(16); break;
		case CmdLine32: m_Hex.SetBytesPerLine(32); break;
		case CmdSearch: Search(false); break;
		case CmdSearchNext: Search(true); break;
		case CmdInt32: SetFormat(Format::Int32); break;
		case CmdFloat: SetFormat(Format::Float); break;
		case CmdDouble: SetFormat(Format::Double); break;
		case CmdAscii: SetFormat(Format::Ascii); break;
		case CmdUnicode: SetFormat(Format::Unicode); break;
	}
}

std::wstring CMemoryView::SaveState() const {
	return std::format(L"d={};l={};f={};e={}", m_Hex.GetDataSize(), m_Hex.GetBytesPerLine(), static_cast<int>(m_Format), (PCWSTR)m_Expression);
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
		else if (item.starts_with(L"f=") && value >= 0 && value <= static_cast<int>(Format::Unicode))
			SetFormat(static_cast<Format>(value));
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
	if (m_SelectAfterRefresh > 0 && m_SelectAfterRefresh <= size)
		m_Hex.SetSelection(0, m_SelectAfterRefresh);
	m_SelectAfterRefresh = 0;
	m_Hex.Refresh();
	if (m_Format != Format::Hex)
		RenderFormatted();
	UpdateWritable();
	RefreshDone();
}

std::vector<BYTE> CMemoryView::ParsePattern(const CString& input) {
	CString text(input);
	text.Trim();
	std::vector<BYTE> bytes;
	if (text.GetLength() >= 3 && (text[0] == L'L' || text[0] == L'l') && text[1] == L'"' && text[text.GetLength() - 1] == L'"') {
		// UTF-16
		auto s = text.Mid(2, text.GetLength() - 3);
		auto p = reinterpret_cast<const BYTE*>((PCWSTR)s);
		bytes.assign(p, p + s.GetLength() * sizeof(wchar_t));
	}
	else if (text.GetLength() >= 2 && text[0] == L'"' && text[text.GetLength() - 1] == L'"') {
		// as the program would have it in a char string: UTF-8 (ASCII is the same)
		auto s = ToUtf8((PCWSTR)text.Mid(1, text.GetLength() - 2));
		bytes.assign(s.begin(), s.end());
	}
	else {
		// hex bytes, with or without spaces between them
		text.Remove(L' ');
		if (text.GetLength() % 2)
			return {};
		for (int i = 0; i < text.GetLength(); i += 2) {
			wchar_t digits[3] = { text[i], text[i + 1], 0 };
			wchar_t* end;
			auto value = wcstoul(digits, &end, 16);
			if (*end)
				return {};
			bytes.push_back(static_cast<BYTE>(value));
		}
	}
	return bytes;
}

Task<void> CMemoryView::Search(bool next) {
	auto alive = m_Lifetime.Watch();
	if (!m_Buffer)
		co_return;
	if (!next || m_SearchPattern.empty()) {
		CInputDlg dlg(L"Search Memory", L"Bytes (4D 5A 90), a string (\"text\") or a UTF-16 string (L\"text\"):", m_SearchText);
		if (dlg.DoModal(m_hWnd) != IDOK || !*alive)
			co_return;
		auto pattern = ParsePattern(dlg.GetText());
		if (pattern.empty()) {
			AtlMessageBox(m_hWnd, L"That is not something to search for: hex bytes, \"text\" or L\"text\"", IDS_TITLE, MB_ICONWARNING);
			co_return;
		}
		m_SearchText = dlg.GetText();
		m_SearchPattern = std::move(pattern);
	}

	const auto start = m_Buffer->GetAddress() + m_Hex.GetCaretOffset() + (next ? 1 : 0);
	ULONG64 found = 0;
	auto hr = co_await DebugManager::DebuggerX().SearchMemory(start, m_SearchPattern, found);
	if (!*alive)
		co_return;
	if (FAILED(hr)) {
		CString text;
		text.Format(L"%s was not found after 0x%llX", (PCWSTR)m_SearchText, start);
		AtlMessageBox(m_hWnd, (PCWSTR)text, IDS_TITLE, MB_ICONINFORMATION);
		co_return;
	}
	// (shown from where it is, selected)
	m_Expression.Format(L"0x%llX", found);
	m_AddressEdit.SetWindowText(m_Expression);
	m_SelectAfterRefresh = static_cast<int64_t>(m_SearchPattern.size());
	RequestRefresh();
}

void CMemoryView::ApplyFormattedColors() {
	using namespace Scintilla;
	auto fore = static_cast<Colour>(::GetSysColor(COLOR_WINDOWTEXT));
	auto back = static_cast<Colour>(::GetSysColor(COLOR_WINDOW));
	m_Formatted->StyleSetFore(static_cast<int>(StylesCommon::Default), fore);
	m_Formatted->StyleSetBack(static_cast<int>(StylesCommon::Default), back);
	m_Formatted->StyleClearAll();
	m_Formatted->SetCaretFore(fore);
	m_Formatted->SetSelBack(true, static_cast<Colour>(::GetSysColor(COLOR_HIGHLIGHT)));
	m_Formatted->SetSelFore(true, static_cast<Colour>(::GetSysColor(COLOR_HIGHLIGHTTEXT)));
}

void CMemoryView::SetFormat(Format format) {
	m_Format = format;
	const bool hex = format == Format::Hex;
	m_Hex.ShowWindow(hex ? SW_SHOW : SW_HIDE);
	m_Formatted.ShowWindow(hex ? SW_HIDE : SW_SHOW);
	if (!hex)
		RenderFormatted();
}

void CMemoryView::RenderFormatted() {
	// a line: the address, then the values (as many as fit in 16 or 32 bytes; text: 64 or 32 characters)
	std::wstring text;
	if (m_Buffer) {
		const auto size = m_Buffer->GetSize();
		const auto data = m_Buffer->GetRawData(0);
		const auto base = m_Buffer->GetAddress();
		const int perLine = m_Format == Format::Ascii || m_Format == Format::Unicode ? 64 : m_Hex.GetBytesPerLine();
		for (int64_t offset = 0; offset < size; offset += perLine) {
			text += std::format(L"{:016X}  ", base + offset);
			const auto end = std::min<int64_t>(offset + perLine, size);
			switch (m_Format) {
				case Format::Int32:
					for (auto i = offset; i + 4 <= end; i += 4)
						text += std::format(L"{:>12}", *reinterpret_cast<const int32_t*>(data + i));
					break;
				case Format::Float:
					for (auto i = offset; i + 4 <= end; i += 4)
						text += std::format(L"{:>16.8g}", *reinterpret_cast<const float*>(data + i));
					break;
				case Format::Double:
					for (auto i = offset; i + 8 <= end; i += 8)
						text += std::format(L"{:>26.17g}", *reinterpret_cast<const double*>(data + i));
					break;
				case Format::Ascii:
					for (auto i = offset; i < end; i++)
						text += data[i] >= 0x20 && data[i] < 0x7F ? static_cast<wchar_t>(data[i]) : L'.';
					break;
				case Format::Unicode:
					for (auto i = offset; i + 2 <= end; i += 2) {
						auto c = *reinterpret_cast<const wchar_t*>(data + i);
						text += c >= 0x20 && !(c >= 0xD800 && c <= 0xDFFF) && c != 0x7F ? c : L'.';
					}
					break;
				default:
					break;
			}
			text += L'\n';
		}
	}
	auto utf8 = ToUtf8(text);
	// (the same place again: it stays scrolled where it was)
	const auto address = m_Buffer ? m_Buffer->GetAddress() : 0;
	auto first = address == m_FormattedAddress ? m_Formatted->FirstVisibleLine() : 0;
	m_FormattedAddress = address;
	m_Formatted->SetReadOnly(false);
	m_Formatted->SetText(utf8.c_str());
	m_Formatted->SetReadOnly(true);
	m_Formatted->SetFirstVisibleLine(first);
}
