#include "pch.h"
#include "AssistantView.h"
#include "AppMessages.h"
#include "AppSettings.h"
#include "ApiKey.h"
#include "AssistantSettingsDlg.h"
#include "resource.h"
#include <format>

using DebugX::Ai::AssistantEvent;
using DebugX::Ai::Approval;
using DebugX::Ai::Transcript;

namespace {
	constexpr wchar_t Dot = 0x00B7;

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

CAssistantView::CAssistantView() : m_Input(this, 1) {
}

HWND CAssistantView::Create(HWND hParent) {
	return BaseFrame::Create(hParent, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
}

LRESULT CAssistantView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	// (no link tooltips: a dbgx: link's address is not for reading)
	m_View.Create(*this, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | MDVS_NOLINKTIPS, 0, TextId);
	// (a line break in a message is a line break, as in a chat)
	m_View.SetParseFlags(MDVF_HARDBREAKS);
	ApplyTextStyle();

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
	KillTimer(RenderTimerId);
	// (a request under way ends, and its thread is waited for)
	m_Assistant.reset();
	handled = FALSE;
	return 0;
}

LRESULT CAssistantView::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
	int cx = GET_X_LPARAM(lParam), cy = GET_Y_LPARAM(lParam);
	if (cx == 0 || cy == 0 || !m_View)
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
	place(m_View, 0, 0, cx, textHeight);
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

LRESULT CAssistantView::OnTimer(UINT, WPARAM wParam, LPARAM, BOOL& handled) {
	if (wParam != RenderTimerId) {
		handled = FALSE;
		return 0;
	}
	KillTimer(RenderTimerId);
	m_RenderPending = false;
	Render();
	return 0;
}

LRESULT CAssistantView::OnFontsChanged(UINT, WPARAM, LPARAM, BOOL&) {
	ApplyTextStyle();
	for (HWND h : { m_Input.m_hWnd, m_Send.m_hWnd, m_New.m_hWnd, m_Settings.m_hWnd, m_Status.m_hWnd })
		::SendMessage(h, WM_SETFONT, reinterpret_cast<WPARAM>(AppFonts::UiFont()), TRUE);
	CRect rc;
	GetClientRect(&rc);
	SendMessage(WM_SIZE, SIZE_RESTORED, MAKELPARAM(rc.Width(), rc.Height()));
	return 0;
}

void CAssistantView::ApplyTextStyle() {
	auto& ui = AppFonts::Spec(AppFonts::Kind::Ui);
	auto& code = AppFonts::Spec(AppFonts::Kind::Code);
	MDVSTYLE style{};
	style.mask = MDVSM_FONT | MDVSM_CODEFONT | MDVSM_FONTSIZE | MDVSM_SPACING | MDVSM_PADDING;
	wcsncpy_s(style.szFont, ui.Face.empty() ? L"Segoe UI" : ui.Face.c_str(), _TRUNCATE);
	wcsncpy_s(style.szCodeFont, code.Face.c_str(), _TRUNCATE);
	// (points to DIPs)
	style.fontSize = ui.PointSize10 / 10.0f * 96.0f / 72.0f;
	style.spacing = 0.7f;
	style.padding = 10;
	m_View.SetTextStyle(style);
}

void CAssistantView::ShowWelcome() {
	m_Transcript.AddNote(L"Ask about what is being debugged -- \"Which threads are waiting, and on what?\", \"Why did it crash?\", "
		L"\"What does the third frame's this point to?\". The assistant looks into the target with the debugger's tools, and asks "
		L"before it runs, steps or changes anything. What it reads of the target is sent to Anthropic's API.");
	if (ApiKey::Get().empty())
		m_Transcript.AddNote(L"It needs an Anthropic API key first: Settings.");
	Render();
}

void CAssistantView::Render() {
	// The view parses the document again (and starts at its top): it goes back where it was -- to the end if the end was
	// showing (the conversation is followed), else to the block at the top
	bool follow = std::exchange(m_FollowNext, false);
	SCROLLINFO si{ sizeof(si), SIF_ALL };
	if (!follow)
		follow = !m_View.GetScrollInfo(SB_VERT, &si) || si.nMax <= 0 || si.nPos + static_cast<int>(si.nPage) >= si.nMax - 8;
	const auto top = m_View.GetTopOffset();
	auto doc = m_Transcript.Markdown();
	m_View.SetText(doc.c_str(), doc.size());
	if (follow)
		m_View.SendMessage(WM_VSCROLL, SB_BOTTOM);
	else
		m_View.ScrollToOffset(top);
}

void CAssistantView::RenderSoon() {
	if (!m_RenderPending) {
		m_RenderPending = true;
		SetTimer(RenderTimerId, RenderDelay);
	}
}

void CAssistantView::OnEvent(const AssistantEvent& e) {
	using Kind = AssistantEvent::Kind;
	switch (e.Type) {
		case Kind::Busy:
			m_Busy = true;
			UpdateControls();
			return;
		case Kind::Idle:
			m_Busy = false;
			UpdateControls();
			// (the last of it, now)
			KillTimer(RenderTimerId);
			m_RenderPending = false;
			Render();
			return;
		case Kind::Usage:
			m_Usage = e;
			UpdateStatus();
			return;
		default:
			break;
	}
	if (!m_Transcript.Apply(e))
		return;
	switch (e.Type) {
		case Kind::Text:
		case Kind::Thinking:
			// (a stream of pieces: a render in a moment, for all of them)
			RenderSoon();
			break;
		case Kind::User:
			// (the user just sent it: the conversation is followed from here)
			m_FollowNext = true;
			[[fallthrough]];
		default:
			KillTimer(RenderTimerId);
			m_RenderPending = false;
			Render();
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
	m_Transcript.Clear();
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
	const std::wstring dot = std::wstring(L" ") + Dot + L" ";
	auto text = settings.AssistantModel() + dot + settings.AssistantEffort() + L" effort";
	const auto in = m_Usage.InputTokens + m_Usage.CacheReadTokens + m_Usage.CacheWriteTokens;
	if (in || m_Usage.OutputTokens)
		text += dot + std::format(L"{} tokens in ({} from the cache), {} out", Tokens(in), Tokens(m_Usage.CacheReadTokens), Tokens(m_Usage.OutputTokens));
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

LRESULT CAssistantView::OnLinkClick(int, LPNMHDR hdr, BOOL&) {
	auto link = reinterpret_cast<const NMMDVLINK*>(hdr);
	if (!link->pszUrl)
		return 0;
	std::wstring_view url(link->pszUrl);
	// a symbol or an address: its code
	if (auto expression = Transcript::LinkTarget(url); !expression.empty()) {
		GoToDisassembly(m_hWnd, expression.c_str());
		return 1;
	}
	// a web page the answer pointed to: the browser
	if (url.starts_with(L"https://") || url.starts_with(L"http://")) {
		::ShellExecuteW(m_hWnd, L"open", link->pszUrl, nullptr, nullptr, SW_SHOWNORMAL);
		return 1;
	}
	// (a #link: the view scrolls)
	return 0;
}

LRESULT CAssistantView::OnViewContextMenu(int, LPNMHDR hdr, BOOL&) {
	auto menuInfo = reinterpret_cast<const NMMDVCONTEXTMENU*>(hdr);
	enum { CmdCopy = 1, CmdSelectAll, CmdNew, CmdSettings };
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(menuInfo->hasSelection ? MF_STRING : MF_GRAYED, CmdCopy, L"&Copy\tCtrl+C");
	menu.AppendMenu(MF_STRING, CmdSelectAll, L"Select &All");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(m_Busy ? MF_GRAYED : MF_STRING, CmdNew, L"&New Conversation");
	menu.AppendMenu(m_Busy ? MF_GRAYED : MF_STRING, CmdSettings, L"&Settings...");
	switch (menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, menuInfo->pt.x, menuInfo->pt.y, m_hWnd)) {
		case CmdCopy: m_View.Copy(); break;
		case CmdSelectAll: m_View.SelectAll(); break;
		case CmdNew: NewConversation(); break;
		case CmdSettings: EditSettings(); break;
	}
	return 1;
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
