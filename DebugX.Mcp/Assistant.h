#pragma once

//
// The Assistant: a conversation with Claude about the target, in DebugX itself (the Assistant pane), with the MCP server's
// tools (McpTools.cpp) -- what Claude Code gets through the MCP server, without it.
//
// The loop is the Messages API's manual one: the conversation (append-only: earlier turns are never edited, which keeps
// the thinking blocks valid and the prompt cache warm) goes out with the tools, the answer streams in (shown as it comes),
// and while it asks for tools they run -- on this thread, through IDebugger, one at a time -- and their results go back.
// A tool that changes the session (run, step, breakpoints, any command) runs only if 'approve' says so.
//
// Threads: everything here is on the thread the Assistant was made on (the UI thread, the debugger's); a request runs on a
// worker thread of its own, whose events come back through a window dispatcher of the Assistant's.
//

#include <functional>
#include <memory>
#include <string>
#include <thread>
#include "ClaudeApi.h"
#include "McpTools.h"

namespace DebugX::Ai {
	struct AssistantOptions {
		std::string Model{ "claude-opus-5-5" };
		std::string Effort{ "medium" };		// low, medium, high, xhigh, max
		int MaxTokens{ 64000 };
		// a conversation whose input reaches this many tokens is summarized server-side (compaction; at least 50000);
		// 0: never
		int CompactAtTokens{ 150000 };
	};

	// what the conversation shows, as it happens
	struct AssistantEvent {
		enum class Kind {
			User,			// the user's message (Text)
			TextStart,		// the model's answer, a block of it: then Text pieces, then BlockEnd
			ThinkingStart,	// a summary of its reasoning: then Thinking pieces, then BlockEnd
			Text,
			Thinking,
			BlockEnd,
			ToolCall,		// Name, Text: its arguments (JSON)
			ToolResult,		// Name, Text, Failed
			Info,			// about the conversation (Text): stopped, retrying, another model went on, ...
			Error,			// it could not go on (Text)
			Busy,			// a turn has started
			Idle,			// it has ended
			Usage,			// the tokens so far
		};
		Kind Type;
		std::wstring Text;
		std::wstring Name;
		bool Failed{ false };
		long long InputTokens{ 0 }, OutputTokens{ 0 }, CacheReadTokens{ 0 }, CacheWriteTokens{ 0 };
	};

	enum class Approval {
		Allow,
		AllowAlways,	// this one, and every other for the rest of the conversation
		Deny,
	};

	class Assistant final {
	public:
		using EventHandler = std::function<void(const AssistantEvent&)>;
		// (called on this thread, while the turn waits: it may show a dialog)
		using Approver = std::function<Approval(const std::wstring& tool, const std::wstring& arguments)>;

		Assistant(Mcp::IMcpHost& host, std::unique_ptr<ITransport> transport, EventHandler onEvent, Approver approve);
		// (ends a request under way, and waits for its thread)
		~Assistant();
		Assistant(const Assistant&) = delete;
		Assistant& operator=(const Assistant&) = delete;

		// from the next request on (the conversation goes on: changing the model or effort does not start a new one)
		void SetOptions(AssistantOptions options);
		// another API key, or another transport, from the next request on
		void SetTransport(std::unique_ptr<ITransport> transport);

		// the user's message: the conversation goes on with it. False while a turn is under way.
		bool Send(std::wstring text);
		// ends the turn: the request under way, or the tools after the one that runs (they are answered as not run)
		void Cancel();
		bool IsBusy() const {
			return m_Busy;
		}
		// a new conversation (not while busy)
		bool Reset();

		// the conversation as it is sent (for the tests)
		const json& Messages() const {
			return m_Messages;
		}
		// the system prompt it is given
		static const char* SystemPrompt();
		// the tools, as the request declares them
		static json ToolDeclarations();

	private:
		struct Poster;
		struct StreamState;
		struct StreamAwaiter;
		struct DelayAwaiter;

		DebugEngine::Task<void> Run();
		DebugEngine::Task<Mcp::ToolResult> RunTool(const json& call, bool invalidInput, std::string rawInput);
		DebugEngine::Task<Mcp::ToolResult> Execute(const Mcp::ToolDef& tool, json args);
		json BuildRequest() const;
		std::vector<std::string> Betas() const;
		void OnUpdate(const MessageBuilder::Update& update);
		void Emit(AssistantEvent::Kind kind, std::wstring text = {}, std::wstring name = {}, bool failed = false);
		Mcp::ToolContext Context();

		Mcp::IMcpHost& m_Host;
		std::unique_ptr<ITransport> m_Transport;
		EventHandler m_OnEvent;
		Approver m_Approve;
		AssistantOptions m_Options;
		std::shared_ptr<Poster> m_Poster;
		std::shared_ptr<bool> m_Alive;
		std::thread m_Worker;
		json m_Messages = json::array();
		bool m_Busy{ false };
		bool m_CancelRequested{ false };
		bool m_AllowAll{ false };
		int m_RunsInFlight{ 0 };
		long long m_InputTokens{ 0 }, m_OutputTokens{ 0 }, m_CacheReadTokens{ 0 }, m_CacheWriteTokens{ 0 };
	};
}
