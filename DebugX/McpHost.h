#pragma once

#include "..\DebugX.Mcp\McpServer.h"

// What DebugX's MCP server (Options > MCP Server) and the Assistant pane ask of DebugX: the debugger, the session's status
// and what is being debugged. The MCP server's tool calls also get a line in the Output pane's log, so that the user sees
// what an AI client looked at, and did ('logCalls'; the Assistant pane shows its own).
class McpHost final : public DebugX::Mcp::IMcpHost {
public:
	explicit McpHost(bool logCalls = true) : m_LogCalls(logCalls) {}

	DebugEngine::IDebugger& Debugger() override;
	DebugEngine::ExecutionStatus Status() override;
	std::wstring TargetName() override;
	void ToolCalled(const std::wstring& tool, const std::wstring& arguments, bool failed) override;

private:
	bool m_LogCalls;
};
