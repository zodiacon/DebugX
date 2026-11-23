#pragma once

//
// The tools the MCP server offers (internal to DebugX.Mcp): each is a coroutine that runs on the debugger's thread, through
// IDebugger, and returns text for the model to read.
//

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include "McpServer.h"

namespace DebugX::Mcp {
	using json = nlohmann::json;

	struct ToolResult {
		std::string Text;		// UTF-8
		bool IsError{ false };
	};

	struct ToolContext {
		IMcpHost& Host;
		DebugEngine::IDebugger& Dbg;
		// the runs (go, run_to, step) a tool has started and that have not stopped yet: the target counts as running
		// meanwhile, whatever the status says -- it is the old one until the engine's StatusChanged comes
		int* RunsInFlight{ nullptr };
	};

	enum class ToolKind {
		// one at a time, in order: a tool may switch the current thread or frame and switch it back
		Serialized,
		// started at once (Break, Go, Step...): the target running is what they are about
		Immediate,
		// the server waits for the target to stop, on its own thread, then adds what StopSummary says (wait_for_stop)
		WaitForStop,
	};

	struct ToolDef {
		const char* Name;
		const char* Title;
		const char* Description;
		json InputSchema;
		bool ReadOnly;
		bool Destructive;
		ToolKind Kind;
		// how long the server waits for the result before it answers that it has not come (the tool goes on); an argument
		// can set it (TimeoutArgument, in seconds), up to MaxTimeoutSeconds
		int TimeoutSeconds;
		const char* TimeoutArgument;
		int MaxTimeoutSeconds;
		// a timeout is not an error (Go: the target is still running): the answer says so
		bool TimeoutIsNormal;
		DebugEngine::Task<ToolResult>(*Run)(ToolContext context, json arguments);
	};

	// the tools, in the order tools/list gives them
	const std::vector<ToolDef>& Tools();
	const ToolDef* FindTool(std::string_view name);

	// What the target is doing now and why it stopped: after Go, Step, Break and wait_for_stop
	DebugEngine::Task<ToolResult> StopSummary(ToolContext context, json arguments);
	// the target is stopped (a local kernel, which never stops, counts as stopped), and no run is in flight
	bool IsStopped(const ToolContext& context);
	bool HasTarget(IMcpHost& host);

	// what the server tells the client at initialize: how to use the tools
	extern const char* const ServerInstructions;
	// what is being debugged and whether it is stopped, in a line ("procs.exe (a live user-mode process), stopped"); at once,
	// without asking the debugger
	std::string SessionLine(const ToolContext& context);

	std::string ToUtf8(std::wstring_view text);
	std::wstring FromUtf8(std::string_view text);
}
