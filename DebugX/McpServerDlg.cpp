#include "pch.h"
#include "McpServerDlg.h"
#include "..\DebugX.Mcp\McpServer.h"
#include <format>

std::wstring CMcpServerDlg::ClaudeCommand(int port, const std::wstring& token) {
	return std::format(L"claude mcp add --transport http --scope user debugx http://127.0.0.1:{}/mcp --header \"Authorization: Bearer {}\"",
		port, token);
}

LRESULT CMcpServerDlg::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
	AdjustOKCancelButtons(IDI_OK, IDI_DELETE);
	CheckDlgButton(IDC_MCP_ENABLE, m_Enabled ? BST_CHECKED : BST_UNCHECKED);
	CEdit(GetDlgItem(IDC_MCP_PORT)).SetLimitText(5);
	SetDlgItemInt(IDC_MCP_PORT, m_Port, FALSE);
	SetDlgItemText(IDC_MCP_TOKEN, m_Token.c_str());
	SetDlgItemText(IDC_MCP_STATUS, m_Status.c_str());
	UpdateCommand();
	CenterWindow(GetParent());
	return TRUE;
}

int CMcpServerDlg::PortText() const {
	BOOL ok = FALSE;
	auto port = GetDlgItemInt(IDC_MCP_PORT, &ok, FALSE);
	return ok && port >= 1 && port <= 65535 ? static_cast<int>(port) : 0;
}

void CMcpServerDlg::UpdateCommand() {
	auto port = PortText();
	std::wstring text;
	if (port) {
		text = ClaudeCommand(port, m_Token);
		text += std::format(L"\r\n\r\nAnother MCP client: Streamable HTTP at http://127.0.0.1:{}/mcp with the header\r\nAuthorization: Bearer {}",
			port, m_Token);
	}
	SetDlgItemText(IDC_MCP_COMMAND, text.c_str());
}

LRESULT CMcpServerDlg::OnPortChanged(WORD, WORD, HWND, BOOL&) {
	UpdateCommand();
	return 0;
}

LRESULT CMcpServerDlg::OnNewToken(WORD, WORD, HWND, BOOL&) {
	auto token = DebugX::Mcp::McpServer::NewToken();
	m_Token = std::wstring(token.begin(), token.end());
	SetDlgItemText(IDC_MCP_TOKEN, m_Token.c_str());
	UpdateCommand();
	return 0;
}

LRESULT CMcpServerDlg::OnCopy(WORD, WORD, HWND, BOOL&) {
	auto port = PortText();
	if (!port) {
		::MessageBeep(MB_ICONWARNING);
		return 0;
	}
	auto text = ClaudeCommand(port, m_Token);
	if (OpenClipboard()) {
		::EmptyClipboard();
		const auto bytes = (text.size() + 1) * sizeof(wchar_t);
		if (auto memory = ::GlobalAlloc(GMEM_MOVEABLE, bytes)) {
			memcpy(::GlobalLock(memory), text.c_str(), bytes);
			::GlobalUnlock(memory);
			if (!::SetClipboardData(CF_UNICODETEXT, memory))
				::GlobalFree(memory);
		}
		::CloseClipboard();
	}
	return 0;
}

LRESULT CMcpServerDlg::OnCloseCmd(WORD, WORD wID, HWND, BOOL&) {
	if (wID == IDOK) {
		auto port = PortText();
		if (!port) {
			AtlMessageBox(m_hWnd, L"The port is a number from 1 to 65535.", IDS_TITLE, MB_ICONWARNING);
			GotoDlgCtrl(GetDlgItem(IDC_MCP_PORT));
			return 0;
		}
		m_Port = port;
		m_Enabled = IsDlgButtonChecked(IDC_MCP_ENABLE) == BST_CHECKED;
	}
	EndDialog(wID);
	return 0;
}
