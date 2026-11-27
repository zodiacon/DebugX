#include "pch.h"
#include "Assistant.h"
#include "Dispatcher.h"
#include <algorithm>
#include <coroutine>

using namespace DebugEngine;
using namespace DebugX::Mcp;

namespace DebugX::Ai {

namespace {
	// rounds of tool calls in one turn, before it stops and leaves it to the user to go on
	constexpr int MaxRounds = 60;
	// tries of a request that failed in a way worth trying again (overloaded, rate limited, the connection)
	constexpr int MaxRetries = 3;

	const char* const System = R"SYS(You are the assistant built into DebugX, a Windows debugger with a graphical front end on the Windows debug engine (DbgEng, the engine behind WinDbg). The user is debugging something in DebugX right now -- a live process, a dump file or a kernel -- and asks you about it. You investigate the target with the tools, and answer.

How to work:
- Each of the user's messages starts with a <debugger_state> note: what is being debugged and whether it is stopped. Most tools need the target stopped. Do not stop (break_into), run (go, run_to, step), or change the session (set_breakpoint, remove_breakpoint, run_command that writes memory, registers or breakpoints) unless the user asked for it or it is plainly needed for what they asked; then say what you are doing and why. The user approves each such call.
- Thread ids in the tools are the debugger's ([N] in list_threads, as WinDbg's ~N), not the system's tid.
- For what a process is doing (a hang, a deadlock, busy threads) start with get_all_call_stacks; a filter ("module!*", "*Lock*") finds the threads in some code.
- get_locals and evaluate show values as the debugger's data model does (dx, C++ expressions). Addresses can be numbers or debugger expressions (module!symbol, @rsp+8, poi(@rcx)).
- run_command takes WinDbg commands: !analyze -v, !locks, !heap -s, dt module!Type address, dps @rsp, ub @rip, !peb, .lastevent, .exr -1, !process 0 0 and !thread for kernels. The !-commands need the Windows Kits debugger's engine; if one fails, say so and do without it.
- Base what you say on what the tools showed. If something cannot be found out from the target, say so rather than guess.

How to answer:
- Lead with the finding, then the evidence: the threads, frames and values that show it. Be concise.
- The answer is shown as Markdown (GitHub's) in a narrow debugger pane: short paragraphs, lists, `code` spans, fenced code blocks for code or output, a small table where it helps; headings only for a long answer.
- Write symbols and addresses exactly as the tools show them, each in a code span (`module!function+0x1a`, `0x7ff612345678`): they become links the user can click to see the code.)SYS";

	// what the summary of a long conversation (compaction) has to keep -- it replaces the default prompt. (No tools: with
	// tools declared, the summarizing step might call one, and then there is no summary.)
	const char* const CompactionInstructions = R"SUM(Summarize this debugging conversation so that it can go on from the summary alone. Keep:
- what the user asked for, and what they want to find out or have done;
- the target (process, dump or kernel) and the state it was left in: stopped or running, where, the current thread;
- every finding, with the exact symbols, addresses, thread ids, values and source locations the tools showed;
- what was ruled out, and what is still open;
- what was changed in the session: breakpoints set or removed, the target run or stepped, memory or registers written.
Write the summary only; do not call any tool.)SUM";

	bool SupportsFallbacks(const std::string& model) {
		// (server-side refusal fallbacks, fallbacks: "default": not Haiku, nor models older than these)
		return model.starts_with("claude-opus-5") || model.starts_with("claude-sonnet-5-5") || model.starts_with("claude-fable-5")
			|| model.starts_with("claude-mythos-5");
	}

	using ToolFunction = Task<ToolResult>(*)(ToolContext, json);

	struct Slot {
		std::optional<ToolResult> Result;
	};

	// runs the tool to its end, wherever its caller has got to meanwhile (a tool may outlast its timeout)
	Task<void> RunInto(ToolFunction function, ToolContext context, json args, std::shared_ptr<Slot> slot) {
		try {
			slot->Result = co_await function(context, args);
		}
		catch (const std::exception& e) {
			slot->Result = ToolResult{ std::string("The arguments are not right: ") + e.what(), true };
		}
	}

	ULONGLONG Now() {
		return ::GetTickCount64();
	}
}

// Posts to the window dispatcher, from any thread, until it is closed (the Assistant is going away)
struct Assistant::Poster {
	std::mutex Lock;
	std::unique_ptr<IDispatcher> Dispatcher;

	bool Post(std::function<void()> func) {
		std::lock_guard lock(Lock);
		return Dispatcher && Dispatcher->Post(std::move(func));
	}
	// (on the dispatcher's thread)
	void Close() {
		std::unique_ptr<IDispatcher> dispatcher;
		{
			std::lock_guard lock(Lock);
			dispatcher = std::move(Dispatcher);
		}
	}
};

struct Assistant::StreamState {
	TransportResult Result;
	MessageBuilder Builder;
};

// sends a request on the worker thread; the coroutine goes on when the answer has ended (its updates shown meanwhile)
struct Assistant::StreamAwaiter {
	Assistant* Self;
	json Request;
	std::vector<std::string> Betas;
	std::shared_ptr<StreamState> State = std::make_shared<StreamState>();

	bool await_ready() const noexcept {
		return false;
	}
	void await_suspend(std::coroutine_handle<> handle) {
		if (Self->m_Worker.joinable())
			Self->m_Worker.join();
		Self->m_Transport->Rearm();
		Self->m_Worker = std::thread([self = Self, poster = Self->m_Poster, alive = Self->m_Alive, state = State, transport = Self->m_Transport.get(),
			request = std::move(Request), betas = std::move(Betas), handle] {
			state->Result = transport->Send(request, betas, [&](const std::string& event, const json& data) {
				auto update = state->Builder.Apply(event, data);
				if (update.Type != MessageBuilder::Update::Kind::None)
					poster->Post([self, alive, update = std::move(update)] {
						if (*alive)
							self->OnUpdate(update);
					});
			});
			poster->Post([handle] { handle.resume(); });
		});
	}
	std::shared_ptr<StreamState> await_resume() const noexcept {
		return State;
	}
};

// a pause, without blocking the thread (a thread-pool timer posts the coroutine's resumption)
struct Assistant::DelayAwaiter {
	std::shared_ptr<Poster> Posts;
	DWORD Milliseconds;

	bool await_ready() const noexcept {
		return false;
	}
	void await_suspend(std::coroutine_handle<> handle) {
		struct Context {
			std::shared_ptr<Poster> Posts;
			std::coroutine_handle<> Handle;
		};
		auto context = new Context{ Posts, handle };
		auto timer = ::CreateThreadpoolTimer([](PTP_CALLBACK_INSTANCE, PVOID parameter, PTP_TIMER timer) {
			std::unique_ptr<Context> c(static_cast<Context*>(parameter));
			c->Posts->Post([h = c->Handle] { h.resume(); });
			::CloseThreadpoolTimer(timer);
		}, context, nullptr);
		if (!timer) {
			delete context;
			Posts->Post([handle] { handle.resume(); });
			return;
		}
		ULARGE_INTEGER due;
		due.QuadPart = static_cast<ULONGLONG>(-static_cast<LONGLONG>(Milliseconds) * 10000);
		FILETIME time{ due.LowPart, due.HighPart };
		::SetThreadpoolTimer(timer, &time, 0, 0);
	}
	void await_resume() const noexcept {}
};

Assistant::Assistant(IMcpHost& host, std::unique_ptr<ITransport> transport, EventHandler onEvent, Approver approve) :
	m_Host(host), m_Transport(std::move(transport)), m_OnEvent(std::move(onEvent)), m_Approve(std::move(approve)),
	m_Poster(std::make_shared<Poster>()), m_Alive(std::make_shared<bool>(true)) {
	CreateWindowDispatcher(m_Poster->Dispatcher);
}

Assistant::~Assistant() {
	*m_Alive = false;
	if (m_Transport)
		m_Transport->Cancel();
	if (m_Worker.joinable())
		m_Worker.join();
	// (what is still to come -- updates, resumptions -- goes nowhere)
	m_Poster->Close();
}

void Assistant::SetOptions(AssistantOptions options) {
	m_Options = std::move(options);
}

void Assistant::SetTransport(std::unique_ptr<ITransport> transport) {
	if (m_Busy)
		return;
	if (m_Worker.joinable())
		m_Worker.join();
	m_Transport = std::move(transport);
}

const char* Assistant::SystemPrompt() {
	return System;
}

json Assistant::ToolDeclarations() {
	static const json declarations = [] {
		json tools = json::array();
		for (auto& t : Tools()) {
			// (eager input streaming: the input is checked here, strictly, before a tool runs -- MessageBuilder)
			tools.push_back({ { "name", t.Name }, { "description", t.Description }, { "input_schema", t.InputSchema },
				{ "eager_input_streaming", true } });
		}
		return tools;
	}();
	return declarations;
}

ToolContext Assistant::Context() {
	return { m_Host, m_Host.Debugger(), &m_RunsInFlight };
}

void Assistant::Emit(AssistantEvent::Kind kind, std::wstring text, std::wstring name, bool failed) {
	if (!m_OnEvent)
		return;
	AssistantEvent e{ kind, std::move(text), std::move(name), failed };
	if (kind == AssistantEvent::Kind::Usage) {
		e.InputTokens = m_InputTokens;
		e.OutputTokens = m_OutputTokens;
		e.CacheReadTokens = m_CacheReadTokens;
		e.CacheWriteTokens = m_CacheWriteTokens;
	}
	m_OnEvent(e);
}

bool Assistant::Send(std::wstring text) {
	if (m_Busy || !m_Transport)
		return false;
	// (what the target is now: a note of its own before the message, appended like the rest -- the conversation is never
	// edited)
	json content = json::array();
	content.push_back({ { "type", "text" }, { "text", "<debugger_state>\n" + SessionLine(Context()) + "\n</debugger_state>" } });
	content.push_back({ { "type", "text" }, { "text", ToUtf8(text) } });
	m_Messages.push_back({ { "role", "user" }, { "content", std::move(content) } });
	Emit(AssistantEvent::Kind::User, std::move(text));
	Run();
	return true;
}

void Assistant::Cancel() {
	if (!m_Busy)
		return;
	m_CancelRequested = true;
	if (m_Transport)
		m_Transport->Cancel();
}

bool Assistant::Reset() {
	if (m_Busy)
		return false;
	m_Messages = json::array();
	m_AllowAll = false;
	m_InputTokens = m_OutputTokens = m_CacheReadTokens = m_CacheWriteTokens = 0;
	Emit(AssistantEvent::Kind::Usage);
	return true;
}

std::vector<std::string> Assistant::Betas() const {
	std::vector<std::string> betas{ "thinking-binding-controls-2026-08-01" };
	if (m_Options.CompactAtTokens > 0)
		betas.push_back("compact-2026-01-12");
	if (SupportsFallbacks(m_Options.Model))
		betas.push_back("server-side-fallback-2026-07-01");
	return betas;
}

json Assistant::BuildRequest() const {
	json request = {
		{ "model", m_Options.Model },
		{ "max_tokens", m_Options.MaxTokens },
		{ "stream", true },
		// (the system prompt and the tools never change in a conversation: the thinking blocks are bound to them)
		{ "system", json::array({ { { "type", "text" }, { "text", System } } }) },
		{ "tools", ToolDeclarations() },
		{ "messages", m_Messages },
		// (a thinking block whose history no longer matches is dropped rather than failing the request -- a safety net: the
		// conversation is append-only)
		{ "thinking", { { "type", "adaptive" }, { "display", "summarized" }, { "block_binding", { { "prefix_mismatch_behavior", "drop_block" } } } } },
		{ "output_config", { { "effort", m_Options.Effort } } },
		// (the conversation so far is cached, request after request)
		{ "cache_control", { { "type", "ephemeral" } } },
	};
	if (m_Options.CompactAtTokens > 0) {
		// A long conversation is summarized server-side once its input reaches the threshold: the answer then starts with a
		// compaction block, which goes back with the rest (the API leaves out what came before it)
		request["context_management"] = { { "edits", json::array({ {
			{ "type", "compact_20260112" },
			{ "trigger", { { "type", "input_tokens" }, { "value", std::max(m_Options.CompactAtTokens, 50000) } } },
			{ "instructions", CompactionInstructions },
		} }) } };
	}
	if (SupportsFallbacks(m_Options.Model))
		request["fallbacks"] = "default";
	return request;
}

void Assistant::OnUpdate(const MessageBuilder::Update& update) {
	using Kind = MessageBuilder::Update::Kind;
	const bool text = update.BlockType == "text", thinking = update.BlockType == "thinking";
	switch (update.Type) {
		case Kind::BlockStart:
			if (text)
				Emit(AssistantEvent::Kind::TextStart);
			else if (thinking)
				Emit(AssistantEvent::Kind::ThinkingStart);
			else if (update.BlockType == "fallback")
				Emit(AssistantEvent::Kind::Info, L"(the model declined; " + FromUtf8(update.Text.empty() ? "another model" : update.Text) + L" went on)");
			else if (update.BlockType == "compaction")
				Emit(AssistantEvent::Kind::Info, L"(the conversation so far was summarized, to keep it within the model's limits)");
			break;
		case Kind::Delta:
			if (text)
				Emit(AssistantEvent::Kind::Text, FromUtf8(update.Text));
			else if (thinking)
				Emit(AssistantEvent::Kind::Thinking, FromUtf8(update.Text));
			break;
		case Kind::BlockStop:
			if (text || thinking)
				Emit(AssistantEvent::Kind::BlockEnd);
			break;
		default:
			break;
	}
}

Task<void> Assistant::Run() {
	auto alive = m_Alive;
	m_Busy = true;
	m_CancelRequested = false;
	Emit(AssistantEvent::Kind::Busy);

	for (int round = 0; round < MaxRounds; round++) {
		auto request = BuildRequest();
		auto betas = Betas();
		std::shared_ptr<StreamState> stream;
		for (int attempt = 0; ; attempt++) {
			stream = co_await StreamAwaiter{ this, request, betas };
			if (!*alive)
				co_return;
			auto& r = stream->Result;
			if (r.Ok || r.Cancelled || m_CancelRequested)
				break;
			// (again only if nothing of the answer has been shown: it would be shown twice)
			if (r.Retryable && attempt < MaxRetries && !stream->Builder.HasContent()) {
				const int seconds = std::clamp(std::max(r.RetryAfterSeconds, 2 << attempt), 1, 60);
				Emit(AssistantEvent::Kind::Info, FromUtf8(std::format("{} -- trying again in {} s", r.Error, seconds)));
				co_await DelayAwaiter{ m_Poster, static_cast<DWORD>(seconds * 1000) };
				if (!*alive)
					co_return;
				if (m_CancelRequested)
					break;
				continue;
			}
			break;
		}

		auto& result = stream->Result;
		if (result.Cancelled || m_CancelRequested) {
			// (what had come of the answer is not part of the conversation)
			Emit(AssistantEvent::Kind::Info, L"Stopped.");
			break;
		}
		if (!result.Ok) {
			Emit(AssistantEvent::Kind::Error, FromUtf8(result.Error.empty() ? std::string("The request failed.") : result.Error));
			break;
		}

		auto message = stream->Builder.Message();
		if (message.contains("usage") && message["usage"].is_object()) {
			auto& usage = message["usage"];
			auto number = [&](const char* key) { return usage.contains(key) && usage[key].is_number() ? usage[key].get<long long>() : 0LL; };
			m_InputTokens += number("input_tokens");
			m_OutputTokens += number("output_tokens");
			m_CacheReadTokens += number("cache_read_input_tokens");
			m_CacheWriteTokens += number("cache_creation_input_tokens");
			// (a compaction's own tokens are only in its iteration, not in the totals above)
			if (usage.contains("iterations") && usage["iterations"].is_array())
				for (auto& iteration : usage["iterations"])
					if (iteration.is_object() && iteration.value("type", "") == "compaction") {
						m_InputTokens += iteration.value("input_tokens", 0LL);
						m_OutputTokens += iteration.value("output_tokens", 0LL);
					}
			Emit(AssistantEvent::Kind::Usage);
		}

		auto stop = message.value("stop_reason", "");
		if (stop == "refusal") {
			// (what came of it is left out of the conversation)
			std::string category;
			if (message.contains("stop_details") && message["stop_details"].is_object() && message["stop_details"].contains("category")
				&& message["stop_details"]["category"].is_string())
				category = message["stop_details"]["category"].get<std::string>();
			Emit(AssistantEvent::Kind::Error, FromUtf8("The model declined to answer this" + (category.empty() ? std::string() : " (" + category + ")")
				+ ". Rephrase the request, or start a new conversation."));
			break;
		}

		auto content = EchoContent(message.value("content", json::array()));
		m_Messages.push_back({ { "role", "assistant" }, { "content", content } });
		std::vector<json> calls;
		for (auto& block : content)
			if (block.value("type", "") == "tool_use")
				calls.push_back(block);
		if (calls.empty()) {
			if (stop == "max_tokens")
				Emit(AssistantEvent::Kind::Info, L"(the answer was cut short: it reached the token limit)");
			break;
		}

		// every call gets its result, run or not -- the next request must answer each of them
		json results = json::array();
		auto invalid = stream->Builder.InvalidInputs();
		for (auto& call : calls) {
			auto id = call.value("id", "");
			ToolResult r;
			if (stop == "max_tokens")
				r = { "Not run: the response was cut short before this call was complete.", true };
			else if (m_CancelRequested)
				r = { "Not run: the user stopped the turn.", true };
			else {
				auto it = invalid.find(id);
				r = co_await RunTool(call, it != invalid.end(), it != invalid.end() ? it->second : std::string());
				if (!*alive)
					co_return;
			}
			results.push_back({ { "type", "tool_result" }, { "tool_use_id", id }, { "content", r.Text.empty() ? std::string("(no output)") : r.Text },
				{ "is_error", r.IsError } });
		}
		m_Messages.push_back({ { "role", "user" }, { "content", std::move(results) } });
		if (stop == "max_tokens") {
			Emit(AssistantEvent::Kind::Error, L"The answer was cut short in the middle of a tool call.");
			break;
		}
		if (m_CancelRequested) {
			Emit(AssistantEvent::Kind::Info, L"Stopped.");
			break;
		}
		if (round == MaxRounds - 1)
			Emit(AssistantEvent::Kind::Info, std::format(L"(stopped after {} rounds of tool calls: send a message to let it go on)", MaxRounds));
	}

	m_Busy = false;
	Emit(AssistantEvent::Kind::Idle);
}

Task<ToolResult> Assistant::RunTool(const json& call, bool invalidInput, std::string rawInput) {
	auto alive = m_Alive;
	auto name = call.value("name", "");
	auto args = call.contains("input") && call["input"].is_object() ? call["input"] : json::object();
	auto wideName = FromUtf8(name);

	if (invalidInput) {
		Emit(AssistantEvent::Kind::ToolCall, FromUtf8(rawInput), wideName);
		ToolResult r{ json{ { "INVALID_JSON", rawInput } }.dump(-1, ' ', false, json::error_handler_t::replace), true };
		Emit(AssistantEvent::Kind::ToolResult, L"Not run: its input was not valid JSON.", wideName, true);
		co_return r;
	}
	auto argsText = FromUtf8(args.dump(-1, ' ', false, json::error_handler_t::replace));
	Emit(AssistantEvent::Kind::ToolCall, argsText, wideName);
	auto tool = FindTool(name);
	if (!tool) {
		Emit(AssistantEvent::Kind::ToolResult, L"No such tool.", wideName, true);
		co_return ToolResult{ "There is no tool named " + name + ".", true };
	}
	if (!tool->ReadOnly && !m_AllowAll) {
		auto approval = m_Approve ? m_Approve(wideName, argsText) : Approval::Deny;
		if (!*alive)
			co_return ToolResult{ "Not run.", true };
		if (approval == Approval::Deny) {
			Emit(AssistantEvent::Kind::ToolResult, L"Not allowed by the user.", wideName, true);
			co_return ToolResult{ "The user did not allow this call. Do not try it again unless the user asks for it.", true };
		}
		if (approval == Approval::AllowAlways)
			m_AllowAll = true;
	}
	auto result = co_await Execute(*tool, std::move(args));
	if (!*alive)
		co_return result;
	Emit(AssistantEvent::Kind::ToolResult, FromUtf8(result.Text), wideName, result.IsError);
	co_return result;
}

Task<ToolResult> Assistant::Execute(const ToolDef& tool, json args) {
	auto alive = m_Alive;
	int timeout = tool.TimeoutSeconds;
	if (tool.TimeoutArgument && args.contains(tool.TimeoutArgument) && args[tool.TimeoutArgument].is_number_integer())
		timeout = std::clamp(args[tool.TimeoutArgument].get<int>(), 0, tool.MaxTimeoutSeconds);
	auto context = Context();

	// (the tool runs to its end on its own; this waits for it as long as the tool's timeout says, or until the user stops)
	auto slot = std::make_shared<Slot>();
	RunInto(tool.Run, context, args, slot);
	auto deadline = Now() + timeout * 1000ull;
	while (!slot->Result && Now() < deadline && !m_CancelRequested) {
		co_await DelayAwaiter{ m_Poster, 50 };
		if (!*alive)
			co_return ToolResult{};
	}
	ToolResult result;
	if (slot->Result)
		result = *slot->Result;
	else if (m_CancelRequested)
		result ={ "The user stopped the turn before this completed; it goes on in DebugX.", true };
	else if (tool.TimeoutIsNormal)
		result = { std::format("The target is still running: it has not stopped within {} s. wait_for_stop waits for it to stop, break_into "
			"stops it.", timeout), false };
	else
		result = { std::format("{} has not completed within {} s and goes on in DebugX (the debugger may be busy: a long command, symbols "
			"loading).", tool.Name, timeout), true };

	if (tool.Kind == ToolKind::WaitForStop && slot->Result && !result.IsError) {
		// Break, then wait; or just wait: until the target stops (or the session ends), then where it is
		deadline = Now() + timeout * 1000ull;
		while (HasTarget(m_Host) && !IsStopped(context)) {
			if (m_CancelRequested)
				co_return ToolResult{ "The user stopped the turn while it waited for the target to stop.", true };
			if (Now() >= deadline)
				co_return ToolResult{ (result.Text.empty() ? std::string() : result.Text + "\n") + std::format("The target is still running: it "
					"has not stopped within {} s.", timeout), false };
			co_await DelayAwaiter{ m_Poster, 100 };
			if (!*alive)
				co_return ToolResult{};
		}
		auto summary = co_await StopSummary(context, json::object());
		if (!*alive)
			co_return ToolResult{};
		result.Text = (result.Text.empty() ? std::string() : result.Text + "\n") + summary.Text;
	}
	co_return result;
}

}
