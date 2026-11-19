#include "stdafx.h"
#include "SourceView.h"
#include "DebugManager.h"
#include "BreakpointDlg.h"
#include "InputDlg.h"
#include "AppMessages.h"
#include <format>
#include <scintilla/ILexer.h>
#include <lexilla/Lexilla.h>
#include <lexilla/SciLexer.h>

using namespace DebugEngine;
using namespace Scintilla;

namespace {
	bool IsCodeFile(const std::wstring& path) {
		auto dot = path.find_last_of(L'.');
		if (dot == std::wstring::npos)
			return false;
		auto ext = path.substr(dot + 1);
		for (auto known : { L"c", L"cpp", L"cxx", L"cc", L"h", L"hpp", L"hxx", L"hh", L"inl", L"ipp", L"tlh", L"tli" }) {
			if (_wcsicmp(ext.c_str(), known) == 0)
				return true;
		}
		return false;
	}

	bool SamePath(const std::wstring& a, const std::wstring& b) {
		return !a.empty() && _wcsicmp(a.c_str(), b.c_str()) == 0;
	}
}

LRESULT CSourceView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_Text.Create(*this, nullptr, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
		WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, TextId);
	m_Text.InitCall();

	m_Text->SetUndoCollection(false);
	m_Text->SetWrapMode(Wrap::None);
	m_Text->SetTabWidth(4);
	m_Text->SetScrollWidthTracking(true);
	// (the mouse resting on a name shows its value)
	m_Text->SetMouseDwellTime(500);
	m_Text->StyleSetFont(static_cast<int>(StylesCommon::Default), "Consolas");
	m_Text->StyleSetSizeFractional(static_cast<int>(StylesCommon::Default), 1000);

	m_IsCode = IsCodeFile(m_Path);
	if (m_IsCode) {
		if (auto lexer = ::CreateLexer("cpp")) {
			m_Text->SetILexer(lexer);
			// (what is defined is not known here: code in an #if the lexer would take for inactive is shown as any other)
			m_Text->SetProperty("lexer.cpp.track.preprocessor", "0");
			m_Text->SetKeyWords(0,
				"alignas alignof and and_eq asm auto bitand bitor bool break case catch char char8_t char16_t char32_t class "
				"compl concept const consteval constexpr constinit const_cast continue co_await co_return co_yield decltype "
				"default delete do double dynamic_cast else enum explicit export extern false float for friend goto if inline "
				"int long mutable namespace new noexcept not not_eq nullptr operator or or_eq private protected public "
				"register reinterpret_cast requires return short signed sizeof static static_assert static_cast struct "
				"switch template this thread_local throw true try typedef typeid typename union unsigned using virtual void "
				"volatile wchar_t while xor xor_eq override final __declspec __cdecl __stdcall __fastcall __vectorcall "
				"__forceinline __inline __int8 __int16 __int32 __int64 __try __except __finally __leave __uuidof __debugbreak");
			m_Text->SetKeyWords(1,
				"BOOL BOOLEAN BYTE CHAR DWORD DWORD64 DWORD_PTR HANDLE HMODULE HINSTANCE HRESULT HWND INT LONG LONG64 LONG_PTR "
				"LPARAM LPCSTR LPCWSTR LPSTR LPVOID LPWSTR LRESULT NTSTATUS PCSTR PCWSTR PSTR PVOID PWSTR SIZE_T UINT UINT_PTR "
				"ULONG ULONG64 ULONG_PTR ULONGLONG USHORT VOID WCHAR WORD WPARAM size_t ptrdiff_t intptr_t uintptr_t "
				"int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t std");
		}
	}

	m_Text->SetMarginTypeN(MarginNumbers, MarginType::Number);
	m_Text->SetMarginTypeN(MarginSymbols, MarginType::Symbol);
	m_Text->SetMarginWidthN(MarginSymbols, 18);
	m_Text->SetMarginMaskN(MarginNumbers, 0);
	m_Text->SetMarginMaskN(MarginSymbols, (1 << MarkerBreakpoint) | (1 << MarkerBreakpointDisabled) | (1 << MarkerCurrent) | (1 << MarkerFrame));
	m_Text->SetMarginSensitiveN(MarginSymbols, true);
	m_Text->SetMarginSensitiveN(MarginNumbers, true);
	m_Text->MarkerDefine(MarkerBreakpoint, MarkerSymbol::Circle);
	m_Text->MarkerDefine(MarkerBreakpointDisabled, MarkerSymbol::Circle);
	m_Text->MarkerDefine(MarkerCurrent, MarkerSymbol::ShortArrow);
	m_Text->MarkerSetFore(MarkerCurrent, static_cast<Colour>(RGB(0, 0, 0)));
	m_Text->MarkerSetBack(MarkerCurrent, static_cast<Colour>(RGB(255, 230, 0)));
	m_Text->MarkerDefine(MarkerFrame, MarkerSymbol::ShortArrow);
	m_Text->MarkerSetFore(MarkerFrame, static_cast<Colour>(RGB(0, 0, 0)));
	m_Text->MarkerSetBack(MarkerFrame, static_cast<Colour>(RGB(80, 200, 80)));
	// the whole line, behind the text
	m_Text->MarkerDefine(MarkerCurrentLine, MarkerSymbol::Background);
	m_Text->MarkerDefine(MarkerFrameLine, MarkerSymbol::Background);

	LoadFile();
	ApplyColors();
	s_Views.push_back(this);
	InitPane();
	return 0;
}

LRESULT CSourceView::OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
	std::erase(s_Views, this);
	ExitPane();
	handled = FALSE;
	return 0;
}

bool CSourceView::LoadFile() {
	std::string text;
	wil::unique_hfile file(::CreateFileW(m_Path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, 0, nullptr));
	LARGE_INTEGER size{};
	bool ok = file && ::GetFileSizeEx(file.get(), &size) && size.QuadPart < 64 << 20;
	if (ok) {
		std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
		DWORD read = 0;
		ok = bytes.empty() || ::ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
		bytes.resize(read);
		text = ToUtf8Text(std::move(bytes));
	}
	if (!ok)
		text = ToUtf8(L"(" + m_Path + L" could not be read)");

	m_Text->SetReadOnly(false);
	m_Text->SetText(text.c_str());
	m_Text->SetReadOnly(true);
	m_Text->EmptyUndoBuffer();

	// room for the widest line number
	auto digits = std::to_string(m_Text->LineCount());
	m_Text->SetMarginWidthN(MarginNumbers, m_Text->TextWidth(static_cast<int>(StylesCommon::LineNumber), ("_" + digits).c_str()) + 4);
	return ok;
}

LRESULT CSourceView::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto cx = GET_X_LPARAM(lParam), cy = GET_Y_LPARAM(lParam);
	if (cx && cy)
		m_Text.MoveWindow(0, 0, cx, cy);
	return 0;
}

LRESULT CSourceView::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
	m_Text.SetFocus();
	return 0;
}

LRESULT CSourceView::OnThemeChanged(UINT, WPARAM, LPARAM, BOOL&) {
	ApplyColors();
	::SetWindowTheme(m_Text, WTLHelper::IsDarkMode() ? L"DarkMode_Explorer" : L"Explorer", nullptr);
	m_Text.Invalidate();
	return 0;
}

void CSourceView::ApplyColors() {
	const bool dark = WTLHelper::IsDarkMode();
	auto fore = static_cast<Colour>(::GetSysColor(COLOR_WINDOWTEXT));
	auto back = static_cast<Colour>(::GetSysColor(COLOR_WINDOW));
	m_Text->StyleSetFore(static_cast<int>(StylesCommon::Default), fore);
	m_Text->StyleSetBack(static_cast<int>(StylesCommon::Default), back);
	m_Text->StyleClearAll();		// (also back to plain text for the lexer's styles below)
	m_Text->StyleSetFore(static_cast<int>(StylesCommon::LineNumber), static_cast<Colour>(::GetSysColor(COLOR_GRAYTEXT)));
	m_Text->StyleSetBack(static_cast<int>(StylesCommon::LineNumber), back);
	m_Text->SetCaretFore(fore);
	m_Text->SetSelBack(true, static_cast<Colour>(::GetSysColor(COLOR_HIGHLIGHT)));
	m_Text->SetSelFore(true, static_cast<Colour>(::GetSysColor(COLOR_HIGHLIGHTTEXT)));
	m_Text->CallTipSetBack(back);
	m_Text->CallTipSetFore(fore);

	auto red = static_cast<Colour>(dark ? RGB(200, 60, 60) : RGB(220, 0, 0));
	m_Text->MarkerSetFore(MarkerBreakpoint, static_cast<Colour>(RGB(255, 255, 255)));
	m_Text->MarkerSetBack(MarkerBreakpoint, red);
	m_Text->MarkerSetFore(MarkerBreakpointDisabled, red);
	m_Text->MarkerSetBack(MarkerBreakpointDisabled, back);
	m_Text->MarkerSetBack(MarkerCurrentLine, static_cast<Colour>(dark ? RGB(74, 68, 24) : RGB(255, 247, 190)));
	m_Text->MarkerSetBack(MarkerFrameLine, static_cast<Colour>(dark ? RGB(30, 64, 36) : RGB(214, 240, 214)));

	if (!m_IsCode)
		return;
	auto setFore = [&](int style, COLORREF light, COLORREF inDark) {
		m_Text->StyleSetFore(style, static_cast<Colour>(dark ? inDark : light));
	};
	for (auto style : { SCE_C_COMMENT, SCE_C_COMMENTLINE, SCE_C_COMMENTDOC, SCE_C_COMMENTLINEDOC, SCE_C_COMMENTDOCKEYWORD, SCE_C_COMMENTDOCKEYWORDERROR })
		setFore(style, RGB(0, 128, 0), RGB(87, 166, 74));
	setFore(SCE_C_NUMBER, RGB(9, 134, 88), RGB(181, 206, 168));
	setFore(SCE_C_WORD, RGB(0, 0, 255), RGB(86, 156, 214));
	setFore(SCE_C_WORD2, RGB(43, 145, 175), RGB(78, 201, 176));
	for (auto style : { SCE_C_STRING, SCE_C_CHARACTER, SCE_C_VERBATIM, SCE_C_STRINGRAW, SCE_C_ESCAPESEQUENCE })
		setFore(style, RGB(163, 21, 21), RGB(214, 157, 133));
	setFore(SCE_C_STRINGEOL, RGB(255, 0, 0), RGB(255, 120, 120));
	setFore(SCE_C_PREPROCESSOR, RGB(128, 128, 128), RGB(155, 155, 155));
	setFore(SCE_C_OPERATOR, RGB(0, 0, 0), RGB(180, 180, 180));
}

void CSourceView::SetMarker(ULONG line, int marker, int lineMarker) {
	if (line == 0 || line > static_cast<ULONG>(m_Text->LineCount()))
		return;
	m_Text->MarkerAdd(line - 1, marker);
	if (lineMarker >= 0)
		m_Text->MarkerAdd(line - 1, lineMarker);
}

void CSourceView::SetLocation(ULONG currentLine, ULONG frameLine, bool show) {
	m_CurrentLine = currentLine;
	m_FrameLine = frameLine;
	for (auto marker : { MarkerCurrent, MarkerCurrentLine, MarkerFrame, MarkerFrameLine })
		m_Text->MarkerDeleteAll(marker);
	SetMarker(currentLine, MarkerCurrent, MarkerCurrentLine);
	SetMarker(frameLine, MarkerFrame, frameLine == currentLine ? -1 : MarkerFrameLine);
	if (show && (frameLine || currentLine))
		GoToLine(frameLine ? frameLine : currentLine);
}

void CSourceView::GoToLine(ULONG line) {
	if (line == 0)
		return;
	auto index = static_cast<Line>(line - 1);
	m_Text->GotoLine(index);
	auto first = m_Text->FirstVisibleLine();
	auto visible = m_Text->LinesOnScreen();
	// (a line that is on show stays where it is; else it goes a third of the way down)
	if (index < first || index >= first + visible)
		m_Text->SetFirstVisibleLine(index > visible / 3 ? index - visible / 3 : 0);
}

ULONG CSourceView::CaretLine() {
	return static_cast<ULONG>(m_Text->LineFromPosition(m_Text->CurrentPos())) + 1;
}

void CSourceView::OnTargetEnded() {
	m_Breakpoints.clear();
	m_Text->MarkerDeleteAll(MarkerBreakpoint);
	m_Text->MarkerDeleteAll(MarkerBreakpointDisabled);
	SetLocation(0, 0, false);
	MarkDirty();
}

Task<void> CSourceView::Refresh() {
	auto alive = m_Lifetime.Watch();

	std::vector<BreakpointInfo> breakpoints;
	co_await DebugManager::DebuggerX().EnumBreakpoints(breakpoints);
	if (!*alive)
		co_return;

	std::vector<ULONG64> offsets;
	std::vector<const BreakpointInfo*> code;
	for (auto& bp : breakpoints) {
		if (!bp.IsData && !bp.Deferred) {
			offsets.push_back(bp.Offset);
			code.push_back(&bp);
		}
	}
	std::vector<SourceLocation> locations;
	if (!offsets.empty())
		co_await DebugManager::DebuggerX().GetSourceLocations(offsets, locations);
	if (!*alive)
		co_return;

	m_Breakpoints.clear();
	for (size_t i = 0; i < locations.size() && i < code.size(); i++) {
		auto& location = locations[i];
		if (location.Line && (SamePath(location.Path, m_Path) || SamePath(location.File, m_Path) || SamePath(location.File, m_SymbolFile)))
			m_Breakpoints.push_back({ code[i]->Id, location.Line, code[i]->Enabled });
	}
	m_Text->MarkerDeleteAll(MarkerBreakpoint);
	m_Text->MarkerDeleteAll(MarkerBreakpointDisabled);
	for (auto& bp : m_Breakpoints)
		SetMarker(bp.Line, bp.Enabled ? MarkerBreakpoint : MarkerBreakpointDisabled, -1);

	RefreshDone();
}

std::wstring CSourceView::LineExpression(ULONG line) const {
	return std::format(L"`{}:{}`", m_SymbolFile.empty() ? m_Path : m_SymbolFile, line);
}

Task<void> CSourceView::ToggleBreakpointAt(ULONG line) {
	auto alive = m_Lifetime.Watch();
	auto& session = DebugManager::Session();
	if (!session.HasTarget() || session.IsRunning()) {
		::MessageBeep(MB_ICONWARNING);
		co_return;
	}

	// (the list changes as soon as one is removed: the ids are taken first)
	std::vector<ULONG> ids;
	for (auto& bp : m_Breakpoints) {
		if (bp.Line == line)
			ids.push_back(bp.Id);
	}
	if (!ids.empty()) {
		for (auto id : ids)
			co_await DebugManager::DebuggerX().RemoveBreakpoint(id);
		co_return;
	}

	BreakpointSettings settings;
	settings.Expression = LineExpression(line);
	auto hr = co_await DebugManager::DebuggerX().AddBreakpoint(settings);
	if (!*alive)
		co_return;
	if (FAILED(hr))
		AtlMessageBox(m_hWnd, (PCWSTR)BreakpointErrorText(settings, hr), IDS_TITLE, MB_ICONERROR);
}

Task<void> CSourceView::RunToLine(ULONG line) {
	if (!DebugManager::Session().CanRun()) {
		::MessageBeep(MB_ICONWARNING);
		co_return;
	}
	auto command = L"g " + LineExpression(line);
	co_await DebugManager::DebuggerX().ExecuteCommand(command.c_str());
}

Task<void> CSourceView::SetNextStatement(ULONG line) {
	auto alive = m_Lifetime.Watch();
	if (!DebugManager::Session().CanRun()) {
		::MessageBeep(MB_ICONWARNING);
		co_return;
	}
	ULONG64 address;
	auto hr = co_await DebugManager::DebuggerX().Evaluate(LineExpression(line).c_str(), address);
	if (!*alive)
		co_return;
	if (FAILED(hr)) {
		AtlMessageBox(m_hWnd, std::format(L"There is no code at line {} (0x{:08X})", line, static_cast<unsigned>(hr)).c_str(), IDS_TITLE, MB_ICONERROR);
		co_return;
	}
	co_await DebugManager::SetNextStatement(m_hWnd, address);
}

LRESULT CSourceView::OnMarginClick(int, LPNMHDR hdr, BOOL&) {
	auto data = reinterpret_cast<const NotificationData*>(hdr);
	ToggleBreakpointAt(static_cast<ULONG>(m_Text->LineFromPosition(data->position)) + 1);
	return 0;
}

LRESULT CSourceView::OnToggleBreakpoint(WORD, WORD, HWND, BOOL&) {
	ToggleBreakpointAt(CaretLine());
	return 0;
}

LRESULT CSourceView::OnRunToCursor(WORD, WORD, HWND, BOOL&) {
	RunToLine(CaretLine());
	return 0;
}

LRESULT CSourceView::OnSetNextStatement(WORD, WORD, HWND, BOOL&) {
	SetNextStatement(CaretLine());
	return 0;
}

LRESULT CSourceView::OnGoTo(WORD, WORD, HWND, BOOL&) {
	auto count = static_cast<ULONG>(m_Text->LineCount());
	CInputDlg dlg(L"Go to Line", std::format(L"Line number (1 - {}):", count).c_str(), std::to_wstring(CaretLine()).c_str());
	if (dlg.DoModal(m_hWnd) != IDOK)
		return 0;
	wchar_t* end;
	auto line = ::wcstoul(dlg.GetText(), &end, 10);
	if (*end != L'\0' || line == 0) {
		::MessageBeep(MB_ICONWARNING);
		return 0;
	}
	GoToLine(std::min(line, count));
	m_Text.SetFocus();
	return 0;
}

std::wstring CSourceView::WatchExpression() {
	auto start = m_Text->SelectionStart(), end = m_Text->SelectionEnd();
	if (start == end) {
		// the word at the caret (letters, digits, _)
		auto pos = m_Text->CurrentPos();
		start = m_Text->WordStartPosition(pos, true);
		end = m_Text->WordEndPosition(pos, true);
	}
	if (start == end || end - start > 200 || m_Text->LineFromPosition(start) != m_Text->LineFromPosition(end))
		return {};
	auto text = FromUtf8(m_Text->StringOfRange({ start, end }));
	auto first = text.find_first_not_of(L" \t"), last = text.find_last_not_of(L" \t");
	if (first == std::wstring::npos)
		return {};
	text = text.substr(first, last - first + 1);
	// (a number or a keyword is not a watch)
	if (iswdigit(text[0]))
		return {};
	return text;
}

std::wstring CSourceView::ExpressionAt(Position pos) {
	if (!m_IsCode)
		return {};

	auto selStart = m_Text->SelectionStart(), selEnd = m_Text->SelectionEnd();
	if (selStart != selEnd && pos >= selStart && pos < selEnd)
		return WatchExpression();

	// a name (or this), not a keyword, a number, or in a comment or a string
	auto isName = [&](Position start, Position end) {
		auto style = m_Text->StyleAt(start);
		return style == SCE_C_IDENTIFIER || (style == SCE_C_WORD && m_Text->StringOfRange({ start, end }) == "this");
	};
	auto start = m_Text->WordStartPosition(pos, true), end = m_Text->WordEndPosition(pos, true);
	if (start == end || end - start > 200 || !isName(start, end))
		return {};

	// what it is a member of, to the left: a.b, p->b, ns::b (not an element: a[i].b is b)
	while (true) {
		auto p = start;
		if (p >= 1 && m_Text->CharAt(p - 1) == '.')
			p -= 1;
		else if (p >= 2 && m_Text->CharAt(p - 1) == '>' && m_Text->CharAt(p - 2) == '-')
			p -= 2;
		else if (p >= 2 && m_Text->CharAt(p - 1) == ':' && m_Text->CharAt(p - 2) == ':')
			p -= 2;
		else
			break;
		auto wordStart = m_Text->WordStartPosition(p, true);
		if (wordStart == p || !isName(wordStart, p) || end - wordStart > 200)
			break;
		start = wordStart;
	}
	return FromUtf8(m_Text->StringOfRange({ start, end }));
}

LRESULT CSourceView::OnDwellStart(int, LPNMHDR hdr, BOOL&) {
	auto data = reinterpret_cast<const NotificationData*>(hdr);
	auto dwell = ++m_Dwell;
	auto& session = DebugManager::Session();
	// (a position of -1: not over text)
	if (data->position < 0 || !session.HasTarget() || session.IsRunning())
		return 0;
	auto expression = ExpressionAt(data->position);
	if (!expression.empty())
		ShowValue(data->position, std::move(expression), dwell);
	return 0;
}

LRESULT CSourceView::OnDwellEnd(int, LPNMHDR, BOOL&) {
	++m_Dwell;
	m_Text->CallTipCancel();
	return 0;
}

Task<void> CSourceView::ShowValue(Position pos, std::wstring expression, ULONG dwell) {
	auto alive = m_Lifetime.Watch();
	std::vector<SymbolEntry> entries;
	auto hr = co_await DebugManager::DebuggerX().EvaluateWatches(entries, { expression }, {});
	// (nothing for what does not evaluate: a type, a function's name, a name out of scope...)
	if (!*alive || dwell != m_Dwell || FAILED(hr) || entries.empty() || entries[0].Error)
		co_return;

	constexpr size_t MaxValue = 300, MaxType = 80;
	auto& e = entries[0];
	auto text = expression + L" = " + (e.Value.size() > MaxValue ? e.Value.substr(0, MaxValue) + L"..." : e.Value);
	if (!e.Type.empty())
		text += L"   [" + (e.Type.size() > MaxType ? e.Type.substr(0, MaxType) + L"...]" : e.Type + L"]");
	m_Text->CallTipShow(pos, ToUtf8(text).c_str());
}

LRESULT CSourceView::OnTextContextMenu(UINT, WPARAM, LPARAM lParam, BOOL&) {
	CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	if (pt.x == -1) {
		// from the keyboard: at the caret
		auto caret = m_Text->CurrentPos();
		pt = { m_Text->PointXFromPosition(caret), m_Text->PointYFromPosition(caret) };
		m_Text.ClientToScreen(&pt);
	}
	else {
		// the line clicked on becomes the caret's (unless the click is in a selection, which stays)
		CPoint client(pt);
		m_Text.ScreenToClient(&client);
		auto pos = m_Text->PositionFromPoint(client.x, client.y);
		if (pos < m_Text->SelectionStart() || pos > m_Text->SelectionEnd())
			m_Text->GotoPos(pos);
	}

	auto& session = DebugManager::Session();
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(session.HasTarget() && !session.IsRunning() ? MF_STRING : (MF_STRING | MF_GRAYED), ID_DISASM_TOGGLEBREAKPOINT, L"Toggle &Breakpoint\tF9");
	menu.AppendMenu(session.CanRun() ? MF_STRING : (MF_STRING | MF_GRAYED), ID_DEBUG_RUNTOCURSOR, L"&Run to Cursor\tCtrl+F10");
	menu.AppendMenu(session.CanRun() ? MF_STRING : (MF_STRING | MF_GRAYED), ID_DEBUG_SETNEXTSTATEMENT, L"Set &Next Statement\tCtrl+Shift+F10");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_EDIT_COPY, L"&Copy\tCtrl+C");
	menu.AppendMenu(MF_STRING, ID_EDIT_GOTO, L"&Go to Line...\tCtrl+G");
	enum { CmdAddWatch = 1 };
	auto watch = WatchExpression();
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(watch.empty() ? (MF_STRING | MF_GRAYED) : MF_STRING, CmdAddWatch,
		watch.empty() ? L"Add to &Watch" : std::format(L"Add to &Watch: {}", watch.size() > 40 ? watch.substr(0, 40) + L"..." : watch).c_str());
	switch (menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd)) {
		case ID_DISASM_TOGGLEBREAKPOINT: ToggleBreakpointAt(CaretLine()); break;
		case ID_DEBUG_RUNTOCURSOR: RunToLine(CaretLine()); break;
		case ID_DEBUG_SETNEXTSTATEMENT: SetNextStatement(CaretLine()); break;
		case ID_EDIT_COPY: m_Text->Copy(); break;
		case ID_EDIT_GOTO: PostMessage(WM_COMMAND, ID_EDIT_GOTO); break;
		case CmdAddWatch: AddToWatch(m_hWnd, watch.c_str()); break;
	}
	return 0;
}
