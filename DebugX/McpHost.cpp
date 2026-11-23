#include "pch.h"
#include "McpHost.h"
#include "DebugManager.h"

DebugEngine::IDebugger& McpHost::Debugger() {
	return DebugManager::Debugger();
}

DebugEngine::ExecutionStatus McpHost::Status() {
	return DebugManager::Session().Status();
}

std::wstring McpHost::TargetName() {
	return DebugManager::Session().TargetName();
}

void McpHost::ToolCalled(const std::wstring& tool, const std::wstring& arguments, bool failed) {
	if (!m_LogCalls)
		return;
	constexpr size_t MaxArguments = 200;
	std::wstring line = L"MCP: " + tool;
	if (arguments != L"{}" && !arguments.empty())
		line += L" " + (arguments.size() > MaxArguments ? arguments.substr(0, MaxArguments) + L"..." : arguments);
	if (failed)
		line += L" (failed)";
	DebugManager::Session().Log(std::move(line));
}
