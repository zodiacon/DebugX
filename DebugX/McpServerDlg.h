#pragma once

#include "resource.h"
#include "DialogHelper.h"

// Options > MCP Server: the server that lets AI clients (Claude Code, any MCP client) look into the target (DebugX.Mcp) --
// on or off, its port, its token -- and the command that adds it to Claude Code, to copy
class CMcpServerDlg :
	public CDialogImpl<CMcpServerDlg>,
	public CDialogHelper<CMcpServerDlg> {
public:
	enum { IDD = IDD_MCPSERVER };

	// 'status': what the server is doing now (running where, or why not)
	CMcpServerDlg(bool enabled, int port, std::wstring token, std::wstring status) :
		m_Enabled(enabled), m_Port(port), m_Token(std::move(token)), m_Status(std::move(status)) {}

	BEGIN_MSG_MAP(CMcpServerDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
		COMMAND_ID_HANDLER(IDC_MCP_NEWTOKEN, OnNewToken)
		COMMAND_ID_HANDLER(IDC_MCP_COPY, OnCopy)
		COMMAND_HANDLER(IDC_MCP_PORT, EN_CHANGE, OnPortChanged)
	END_MSG_MAP()

	bool Enabled() const {
		return m_Enabled;
	}
	int Port() const {
		return m_Port;
	}
	const std::wstring& Token() const {
		return m_Token;
	}

	// the command that adds the server to Claude Code (user scope: in every project)
	static std::wstring ClaudeCommand(int port, const std::wstring& token);

private:
	LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCloseCmd(WORD, WORD wID, HWND, BOOL&);
	LRESULT OnNewToken(WORD, WORD, HWND, BOOL&);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&);
	LRESULT OnPortChanged(WORD, WORD, HWND, BOOL&);

	// the port in the edit control; 0 if it is not one
	int PortText() const;
	void UpdateCommand();

	bool m_Enabled;
	int m_Port;
	std::wstring m_Token;
	std::wstring m_Status;
};
