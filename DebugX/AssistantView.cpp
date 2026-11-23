#include "pch.h"
#include "AssistantView.h"
#include "AppMessages.h"
#include "AppSettings.h"
#include "ApiKey.h"
#include "AssistantSettingsDlg.h"
#include "resource.h"
#include <regex>
#include <format>

using namespace Scintilla;
using DebugX::Ai::AssistantEvent;
using DebugX::Ai::Approval;

namespace {
	// a tool result shows its first lines; the model gets all of it
	constexpr size_t PreviewLines = 6;
	constexpr size_t MaxArgumentsShown = 240;

	std::wstring Tokens(long long count) {
		if (count < 1000)
			return std::to_wstring(count);
		if (count < 1000000)
			return std::format(L"{:.1f}K", count / 1000.0);
		return std::format(L"{:.2f}M", count / 1000000.0);
	}

	std::string Narrow(const std::wstring& text) {
		return std::string(CW2A(text.c_str(), CP_UTF8));
	}
}

CAssistantView::CAssistantView() : m_Text(this, 2), m_Input(this, 1) {
}

HWND CAssistantView::Create(HWND hParent) {
	return BaseFrame::Create(hParent, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
}

LRESULT CAssistantView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_Text.Create(*this, nullptr, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, TextId);
	m_Text.InitCall();
	m_Text->SetReadOnly(true);
	m_Text->SetUndoCollection(false);
	for (int margin = 0; margin < 3; margin++)
		m_Text->SetMarginWidthN(margin, 0);
	m_Text->SetMarginLeft(6);
	m_Text->SetMarginRight(6);
	m_Text->SetWrapMode(Wrap::Word);
	m_Text->SetWrapIndentMode(WrapIndentMode::Fixed);
	m_Text->UsePopUp(PopUp::Never);
	m_Text->SetHotspotActiveUnderline(true);
	ApplyStyles();

	m_Input.Create(*this, nullptr, nullptr, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
		0, InputId);
	m_Input.SetFont(AppFonts::UiFont());
	m_Input.SetLimitText(20000);
	m_Input.SetCueBannerText(L"Ask about the target (Enter sends, Shift+Enter: a new line)");
	// (no WS_BORDER: a flat button draws its own frame)
	m_Send.Create(*this, nullptr, L"Send", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_FLAT, 0, SendId);
	m_New.Create(*this, nullptr, L"New", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_FLAT, 0, NewId);
	m_Settings.Create(*this, nullptr, L"Settings...", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_FLAT, 0, SettingsId);
	m_Status.Create(*this, nullptr, nullptr, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_ENDELLIPSIS | SS_CENTERIMAGE);
	for (HWND h : { m_Send.m_hWnd, m_New.m_hWnd, m_Settings.m_hWnd, m_Status.m_hWnd })
		::SendMessage(h, WM_SETFONT, reinterpret_cast<WPARAM>(AppFonts::UiFont()), TRUE);

	m_Assistant = std::make_unique<DebugX::Ai::Assistant>(m_Host, nullptr,
		[this](const AssistantEvent& e) { OnEvent(e); },
		[this](const std::wstring& tool, const std::wstring& arguments) { return Approve(tool, arguments); });
	ApplySettings();
	ShowWelcome();
	UpdateControls();
	return 0;
}

LRESULT CAssistantView::OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
	// (a request under way ends, and its thread is waited for)
	m_Assistant.reset();
	handled = FALSE;
	return 0;
}

LRESULT CAssistantView::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
	int cx = GET_X_LPARAM(lParam), cy = GET_Y_LPARAM(lParam);
	if (cx == 0 || cy == 0 || !m_Text)
		return 0;
	const int line = AppFonts::TextHeight(AppFonts::UiFont());
	const int bar = line + 10;
	const int inputHeight = 3 * line + 10;
	const int sendWidth = std::max(64, AppFonts::TextWidth(AppFonts::UiFont(), L"Send") + 28);
	const int newWidth = std::max(56, AppFonts::TextWidth(AppFonts::UiFont(), L"New") + 28);
	const int settingsWidth = std::max(80, AppFonts::TextWidth(AppFonts::UiFont(), L"Settings...") + 24);
	const int textHeight = std::max(0, cy - inputHeight - bar - 4);

	// all at once, then everything painted again: a multiline edit (and a button) does not repaint all of itself when it
	// moves or changes size, and showed what had been there before
	const int y = textHeight + 2, inputY = y + bar;
	auto dwp = ::BeginDeferWindowPos(6);
	auto place = [&](HWND h, int x, int top, int width, int height) {
		if (dwp)
			dwp = ::DeferWindowPos(dwp, h, nullptr, x, top, std::max(0, width), std::max(0, height), SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
	};
	place(m_Text, 0, 0, cx, textHeight);
	place(m_Settings, cx - settingsWidth - 2, y, settingsWidth, bar - 2);
	place(m_New, cx - settingsWidth - newWidth - 6, y, newWidth, bar - 2);
	place(m_Status, 6, y, cx - settingsWidth - newWidth - 16, bar - 2);
	place(m_Input, 2, inputY, cx - sendWidth - 8, inputHeight);
	place(m_Send, cx - sendWidth - 2, inputY, sendWidth, inputHeight);
	if (dwp)
		::EndDeferWindowPos(dwp);
	RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
	return 0;
}

LRESULT CAssistantView::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
	m_Input.SetFocus();
	return 0;
}

LRESULT CAssistantView::OnThemeChanged(UINT, WPARAM, LPARAM, BOOL&) {
	ApplyStyles();
	::SetWindowTheme(m_Text, WTLHelper::IsDarkMode() ? L"DarkMode_Explorer" : L"Explorer", nullptr);
	m_Text.Invalidate();
	return 0;
}

LRESULT CAssistantView::OnFontsChanged(UINT, WPARAM, LPARAM, BOOL&) {
	ApplyStyles();
	for (HWND h : { m_Input.m_hWnd, m_Send.m_hWnd, m_New.m_hWnd, m_Settings.m_hWnd, m_Status.m_hWnd })
		::SendMessage(h, WM_SETFONT, reinterpret_cast<WPARAM>(AppFonts::UiFont()), TRUE);
	CRect rc;
	GetClientRect(&rc);
	SendMessage(WM_SIZE, SIZE_RESTORED, MAKELPARAM(rc.Width(), rc.Height()));
	return 0;
}

void CAssistantView::ApplyStyles() {
	const bool dark = WTLHelper::IsDarkMode();
	auto color = [](COLORREF c) { return static_cast<Colour>(c); };
	const auto fore = ::GetSysColor(COLOR_WINDOWTEXT), back = ::GetSysColor(COLOR_WINDOW), gray = ::GetSysColor(COLOR_GRAYTEXT);

	// the code font for everything first (StyleClearAll copies the default style), then the conversation's own text in the
	// UI font; tool calls and their results keep the code font
	AppFonts::ApplyCodeFont(m_Text.Call());
	m_Text->StyleSetFore(static_cast<int>(StylesCommon::Default), color(fore));
	m_Text->StyleSetBack(static_cast<int>(StylesCommon::Default), color(back));
	m_Text->StyleClearAll();
	auto& ui = AppFonts::Spec(AppFonts::Kind::Ui);
	auto face = Narrow(ui.Face.empty() ? std::wstring(L"Segoe UI") : ui.Face);
	for (int style : { StyleText, StyleUser, StyleThinking, StyleInfo, StyleError }) {
		m_Text->StyleSetFont(style, face.c_str());
		m_Text->StyleSetSizeFractional(style, ui.PointSize10 * 10);
	}
	m_Text->StyleSetBold(StyleUser, true);
	m_Text->StyleSetFore(StyleUser, color(dark ? RGB(120, 185, 255) : RGB(0, 84, 166)));
	m_Text->StyleSetItalic(StyleThinking, true);
	m_Text->StyleSetFore(StyleThinking, color(gray));
	m_Text->StyleSetFore(StyleToolCall, color(dark ? RGB(140, 200, 140) : RGB(0, 110, 50)));
	m_Text->StyleSetFore(StyleToolResult, color(gray));
	m_Text->StyleSetFore(StyleError, color(dark ? RGB(255, 120, 120) : RGB(190, 0, 0)));
	m_Text->StyleSetItalic(StyleInfo, true);
	m_Text->StyleSetFore(StyleInfo, color(gray));
	m_Text->StyleSetFore(StyleLink, color(dark ? RGB(102, 178, 255) : RGB(0, 0, 238)));
	m_Text->StyleSetUnderline(StyleLink, true);
	m_Text->StyleSetHotSpot(StyleLink, true);
	m_Text->SetCaretFore(color(fore));
	m_Text->SetSelBack(true, color(::GetSysColor(COLOR_HIGHLIGHT)));
}

void CAssistantView::ShowWelcome() {
	AppendLine(L"Ask about what is being debugged -- \"Which threads are waiting, and on what?\", \"Why did it crash?\", "
		L"\"What does the third frame's this point to?\". The assistant looks into the target with the debugger's tools, and asks "
		L"before it runs, steps or changes anything. What it reads of the target is sent to Anthropic's API.", StyleInfo);
	if (ApiKey::Get().empty())
		AppendLine(L"It needs an Anthropic API key first: Settings.", StyleInfo);
}

void CAssistantView::Append(std::wstring_view text, int style) {
	if (text.empty())
		return;
	// (the view follows the end while the caret is there; a click elsewhere stops it, Ctrl+End goes back)
	const bool follow = m_Text->CurrentPos() == m_Text->Length() && m_Text->SelectionEmpty();
	auto utf8 = ToUtf8(text);
	const auto start = m_Text->Length();
	m_Text->SetReadOnly(false);
	m_Text->AppendText(static_cast<Position>(utf8.size()), utf8.c_str());
	m_Text->SetReadOnly(true);
	m_Text->StartStyling(start, 0);
	m_Text->SetStyling(static_cast<Position>(utf8.size()), style);
	if (follow) {
		m_Text->DocumentEnd();
		m_Text->ScrollCaret();
	}
}

void CAssistantView::AppendLine(std::wstring_view text, int style) {
	EndLine();
	Append(text, style);
	Append(L"\n", style);
}

void CAssistantView::EndLine() {
	auto length = m_Text->Length();
	if (length > 0 && m_Text->CharAt(length - 1) != '\n')
		Append(L"\n", StyleText);
}

void CAssistantView::MarkLinks(Position start) {
	// module!function+0x10, 00007ff6`12345678, 0x7ff612345678
	static const std::regex pattern(R"(([A-Za-z_][\w.\-]*![A-Za-z_?$@~][\w:<>~$@?]*(\+0x[0-9A-Fa-f]+)?)|\b[0-9A-Fa-f]{8}`[0-9A-Fa-f]{8}\b|\b0x[0-9A-Fa-f]{6,16}\b)");
	const auto end = m_Text->Length();
	if (end <= start)
		return;
	auto text = m_Text->StringOfRange(Span(start, end));
	for (auto it = std::sregex_iterator(text.begin(), text.end(), pattern); it != std::sregex_iterator(); ++it) {
		const auto position = start + static_cast<Position>(it->position());
		const auto length = static_cast<Position>(it->length());
		m_Text->StartStyling(position, 0);
		m_Text->SetStyling(length, StyleLink);
		m_Links.push_back({ position, position + length, FromUtf8(it->str()) });
	}
}

void CAssistantView::OnEvent(const AssistantEvent& e) {
	using Kind = AssistantEvent::Kind;
	switch (e.Type) {
		case Kind::User:
			EndLine();
			if (m_Text->Length() > 0)
				Append(L"\n", StyleText);
			AppendLine(L"> " + e.Text, StyleUser);
			Append(L"\n", StyleText);
			// (the user just sent it: follow the conversation from here)
			m_Text->DocumentEnd();
			m_Text->ScrollCaret();
			break;

		case Kind::TextStart:
			EndLine();
			m_BlockStart = m_Text->Length();
			break;
		case Kind::Text:
			Append(e.Text, StyleText);
			break;
		case Kind::ThinkingStart:
			EndLine();
			m_BlockStart = m_Text->Length();
			break;
		case Kind::Thinking:
			// (a block that says nothing shows nothing)
			if (m_Text->Length() == m_BlockStart)
				Append(L"(thinking) ", StyleThinking);
			Append(e.Text, StyleThinking);
			break;
		case Kind::BlockEnd:
			MarkLinks(m_BlockStart);
			EndLine();
			break;

		case Kind::ToolCall: {
			auto args = e.Text == L"{}" ? std::wstring() : e.Text;
			if (args.size() > MaxArgumentsShown)
				args = args.substr(0, MaxArgumentsShown) + L"...";
			AppendLine(L"\u25B8 " + e.Name + (args.empty() ? L"" : L" " + args), StyleToolCall);
			break;
		}
		case Kind::ToolResult: {
			EndLine();
			const auto start = m_Text->Length();
			size_t lines = 0, pos = 0;
			std::wstring preview;
			while (pos < e.Text.size() && lines < PreviewLines) {
				auto end = e.Text.find(L'\n', pos);
				if (end == std::wstring::npos)
					end = e.Text.size();
				auto line = e.Text.substr(pos, end - pos);
				if (!line.empty() && line.back() == L'\r')
					line.pop_back();
				preview += L"    " + line + L"\n";
				lines++;
				pos = end + 1;
			}
			Append(preview.empty() ? L"    (nothing)\n" : preview, e.Failed ? StyleError : StyleToolResult);
			if (pos < e.Text.size()) {
				const auto more = std::count(e.Text.begin() + pos, e.Text.end(), L'\n') + 1;
				AppendLine(std::format(L"    ... ({} more line{})", more, more == 1 ? L"" : L"s"), StyleInfo);
			}
			MarkLinks(start);
			break;
		}

		case Kind::Info:
			AppendLine(e.Text, StyleInfo);
			break;
		case Kind::Error:
			AppendLine(e.Text, StyleError);
			break;
		case Kind::Busy:
			m_Busy = true;
			UpdateControls();
			break;
		case Kind::Idle:
			m_Busy = false;
			UpdateControls();
			break;
		case Kind::Usage:
			m_Usage = e;
			UpdateStatus();
			break;
	}
}

Approval CAssistantView::Approve(const std::wstring& tool, const std::wstring& arguments) {
	if (AppSettings::Get().AssistantAllowControl())
		return Approval::Allow;
	auto def = DebugX::Mcp::FindTool(Narrow(tool));
	std::wstring what = def ? std::wstring(CA2W(def->Description, CP_UTF8)) : std::wstring();
	if (auto period = what.find(L". "); period != std::wstring::npos)
		what.resize(period + 1);
	auto args = arguments.size() > 600 ? arguments.substr(0, 600) + L"..." : arguments;
	auto main = std::format(L"The assistant wants to use {}", tool);
	auto content = what + (args == L"{}" ? L"" : L"\n\n" + args);

	enum { Allow = 100, AllowAll, Deny };
	const TASKDIALOG_BUTTON buttons[] = {
		{ Allow, L"&Allow" },
		{ AllowAll, L"Allow &everything for this conversation" },
		{ Deny, L"&Don't allow" },
	};
	TASKDIALOGCONFIG config{ sizeof(config) };
	config.hwndParent = ::GetAncestor(m_hWnd, GA_ROOT);
	config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
	config.pszWindowTitle = L"DebugX Assistant";
	config.pszMainIcon = TD_WARNING_ICON;
	config.pszMainInstruction = main.c_str();
	config.pszContent = content.c_str();
	config.cButtons = _countof(buttons);
	config.pButtons = buttons;
	config.nDefaultButton = Allow;
	int button = Deny;
	if (FAILED(::TaskDialogIndirect(&config, &button, nullptr, nullptr)))
		return Approval::Deny;
	return button == Allow ? Approval::Allow : button == AllowAll ? Approval::AllowAlways : Approval::Deny;
}

void CAssistantView::ApplySettings() {
	auto& settings = AppSettings::Get();
	DebugX::Ai::AssistantOptions options;
	options.Model = Narrow(settings.AssistantModel());
	options.Effort = Narrow(settings.AssistantEffort());
	m_Assistant->SetOptions(options);
	auto key = ApiKey::Get();
	if (key != m_Key && !m_Busy) {
		m_Assistant->SetTransport(key.empty() ? nullptr : DebugX::Ai::CreateHttpTransport(Narrow(key)));
		m_Key = key;
	}
	UpdateStatus();
}

bool CAssistantView::EditSettings() {
	CAssistantSettingsDlg dlg;
	if (dlg.DoModal(::GetAncestor(m_hWnd, GA_ROOT)) != IDOK)
		return false;
	ApplySettings();
	return true;
}

void CAssistantView::Send() {
	if (m_Busy) {
		m_Assistant->Cancel();
		return;
	}
	CString text;
	m_Input.GetWindowText(text);
	text.Trim();
	if (text.IsEmpty())
		return;
	if (ApiKey::Get().empty() && (!EditSettings() || ApiKey::Get().empty()))
		return;
	ApplySettings();
	if (m_Assistant->Send((PCWSTR)text))
		m_Input.SetWindowText(L"");
}

void CAssistantView::NewConversation() {
	if (m_Busy || !m_Assistant->Reset())
		return;
	m_Text->SetReadOnly(false);
	m_Text->ClearAll();
	m_Text->SetReadOnly(true);
	m_Links.clear();
	ShowWelcome();
	m_Input.SetFocus();
}

void CAssistantView::UpdateControls() {
	m_Send.SetWindowText(m_Busy ? L"Stop" : L"Send");
	m_New.EnableWindow(!m_Busy);
	m_Settings.EnableWindow(!m_Busy);
}

void CAssistantView::UpdateStatus() {
	auto& settings = AppSettings::Get();
	auto text = settings.AssistantModel() + L" \u00B7 " + settings.AssistantEffort() + L" effort";
	const auto in = m_Usage.InputTokens + m_Usage.CacheReadTokens + m_Usage.CacheWriteTokens;
	if (in || m_Usage.OutputTokens)
		text += std::format(L" \u00B7 {} tokens in ({} from the cache), {} out", Tokens(in), Tokens(m_Usage.CacheReadTokens), Tokens(m_Usage.OutputTokens));
	m_Status.SetWindowText(text.c_str());
}

LRESULT CAssistantView::OnSend(WORD, WORD, HWND, BOOL&) {
	Send();
	m_Input.SetFocus();
	return 0;
}

LRESULT CAssistantView::OnNewConversation(WORD, WORD, HWND, BOOL&) {
	NewConversation();
	return 0;
}

LRESULT CAssistantView::OnSettings(WORD, WORD, HWND, BOOL&) {
	EditSettings();
	return 0;
}

LRESULT CAssistantView::OnHotspotClick(int, LPNMHDR hdr, BOOL&) {
	auto data = reinterpret_cast<const NotificationData*>(hdr);
	for (auto& link : m_Links) {
		if (data->position >= link.Start && data->position < link.End) {
			GoToDisassembly(m_hWnd, link.Expression.c_str());
			break;
		}
	}
	return 0;
}

LRESULT CAssistantView::OnInputChar(UINT, WPARAM wParam, LPARAM, BOOL& handled) {
	// Enter sends; Shift+Enter is a line break (the edit's own)
	if (wParam == VK_RETURN && !(::GetKeyState(VK_SHIFT) & 0x8000)) {
		Send();
		return 0;
	}
	handled = FALSE;
	return 0;
}

LRESULT CAssistantView::OnInputKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& handled) {
	if (wParam == VK_ESCAPE && m_Busy) {
		m_Assistant->Cancel();
		return 0;
	}
	handled = FALSE;
	return 0;
}

LRESULT CAssistantView::OnTextContextMenu(UINT, WPARAM, LPARAM lParam, BOOL&) {
	CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	if (pt.x == -1 && pt.y == -1) {
		auto pos = m_Text->CurrentPos();
		pt.SetPoint(m_Text->PointXFromPosition(pos), m_Text->PointYFromPosition(pos));
		m_Text.ClientToScreen(&pt);
	}
	enum { CmdCopy = 1, CmdSelectAll, CmdNew, CmdSettings };
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(m_Text->SelectionEmpty() ? MF_GRAYED : MF_STRING, CmdCopy, L"&Copy\tCtrl+C");
	menu.AppendMenu(MF_STRING, CmdSelectAll, L"Select &All");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(m_Busy ? MF_GRAYED : MF_STRING, CmdNew, L"&New Conversation");
	menu.AppendMenu(m_Busy ? MF_GRAYED : MF_STRING, CmdSettings, L"&Settings...");
	switch (menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd)) {
		case CmdCopy: m_Text->Copy(); break;
		case CmdSelectAll: m_Text->SelectAll(); break;
		case CmdNew: NewConversation(); break;
		case CmdSettings: EditSettings(); break;
	}
	return 0;
}
