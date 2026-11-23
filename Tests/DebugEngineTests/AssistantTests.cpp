// The Assistant (DebugX.Mcp): the Messages API's streamed events made into messages, and the conversation's loop -- tools
// run on a live target (procs.exe), approvals, a retry, a refusal, invalid tool input -- against a scripted transport that
// plays answers as the API streams them. With DEBUGENGINETESTS_LIVE_API=1 and ANTHROPIC_API_KEY set, one question also goes
// to the real API (billed; SKIP otherwise).
#include "Harness.h"
#include <coroutine>
#include <functional>
#include <thread>
#include <nlohmann/json.hpp>
#include "Assistant.h"

using namespace DebugEngine;
using namespace DebugX::Ai;
using json = nlohmann::json;

namespace {
	class Host final : public DebugX::Mcp::IMcpHost {
	public:
		explicit Host(DebuggerX& dbg) : m_Dbg(dbg) {}
		IDebugger& Debugger() override {
			return m_Dbg;
		}
		ExecutionStatus Status() override {
			return g_Events.Status;
		}
		std::wstring TargetName() override {
			return L"procs.exe";
		}

	private:
		DebuggerX& m_Dbg;
	};

	// a pause of the suite's coroutine, with the message loop running meanwhile
	struct Pause {
		DWORD Milliseconds;
		IDispatcher* Dispatcher;
		bool await_ready() const noexcept {
			return false;
		}
		void await_suspend(std::coroutine_handle<> handle) {
			std::thread([ms = Milliseconds, d = Dispatcher, handle] {
				::Sleep(ms);
				d->Post([handle] { handle.resume(); });
				}).detach();
		}
		void await_resume() const noexcept {}
	};

	// SSE text for a message, as the API streams it: each block started, its pieces, stopped; then the stop reason
	std::string Stream(const std::vector<json>& blocks, const std::string& stopReason, json stopDetails = nullptr) {
		std::string sse;
		auto event = [&](const char* name, const json& data) {
			sse += std::string("event: ") + name + "\r\ndata: " + data.dump() + "\r\n\r\n";
		};
		event("message_start", { { "type", "message_start" }, { "message", { { "id", "msg_test" }, { "type", "message" }, { "role", "assistant" },
			{ "model", "claude-opus-5-5" }, { "content", json::array() }, { "usage", { { "input_tokens", 100 }, { "cache_read_input_tokens", 40 } } } } } });
		sse += ": a comment line\n\n";
		sse += "event: ping\ndata: {\"type\": \"ping\"}\n\n";
		for (size_t i = 0; i < blocks.size(); i++) {
			auto& b = blocks[i];
			auto type = b.value("type", "");
			json start = b;
			if (type == "text")
				start["text"] = "";
			else if (type == "thinking") {
				start["thinking"] = "";
				start["signature"] = "";
			}
			else if (type == "tool_use")
				start["input"] = json::object();
			event("content_block_start", { { "type", "content_block_start" }, { "index", i }, { "content_block", start } });
			auto pieces = [&](const std::string& text, const char* deltaType, const char* field) {
				for (size_t p = 0; p < text.size(); p += 5)
					event("content_block_delta", { { "type", "content_block_delta" }, { "index", i }, { "delta", { { "type", deltaType }, { field, text.substr(p, 5) } } } });
			};
			if (type == "text")
				pieces(b.value("text", ""), "text_delta", "text");
			else if (type == "thinking") {
				pieces(b.value("thinking", ""), "thinking_delta", "thinking");
				event("content_block_delta", { { "type", "content_block_delta" }, { "index", i }, { "delta", { { "type", "signature_delta" }, { "signature", b.value("signature", "") } } } });
			}
			else if (type == "tool_use")
				pieces(b.contains("raw") ? b["raw"].get<std::string>() : b["input"].dump(), "input_json_delta", "partial_json");
			event("content_block_stop", { { "type", "content_block_stop" }, { "index", i } });
		}
		json delta = { { "stop_reason", stopReason } };
		if (!stopDetails.is_null())
			delta["stop_details"] = stopDetails;
		event("message_delta", { { "type", "message_delta" }, { "delta", delta }, { "usage", { { "output_tokens", 50 } } } });
		event("message_stop", { { "type", "message_stop" } });
		return sse;
	}

	// plays one script per request, through an SseParser, in small pieces
	class ScriptedTransport final : public ITransport {
	public:
		struct Script {
			std::string Sse;
			bool Fail{ false };
			TransportResult Failure;
		};
		std::vector<Script> Scripts;
		std::vector<json> Requests;
		std::vector<std::vector<std::string>> Betas;

		TransportResult Send(const json& request, const std::vector<std::string>& betas,
			const std::function<void(const std::string& event, const json& data)>& onEvent) override {
			Requests.push_back(request);
			Betas.push_back(betas);
			if (m_Next >= Scripts.size())
				return { false, false, 0, "no more scripts" };
			auto& script = Scripts[m_Next++];
			if (script.Fail)
				return script.Failure;
			SseParser parser;
			bool stopped = false;
			for (size_t i = 0; i < script.Sse.size(); i += 7)
				parser.Feed(std::string_view(script.Sse).substr(i, 7), [&](const std::string& event, const std::string& data) {
					if (event == "message_stop")
						stopped = true;
					onEvent(event, json::parse(data));
				});
			TransportResult result;
			result.Ok = stopped;
			result.HttpStatus = 200;
			return result;
		}
		void Cancel() override {}
		void Rearm() override {}

	private:
		size_t m_Next{ 0 };
	};

	// the messages of one request are the previous request's and more: nothing before was changed
	bool IsPrefix(const json& before, const json& after) {
		if (!before.is_array() || !after.is_array() || before.size() > after.size())
			return false;
		for (size_t i = 0; i < before.size(); i++)
			if (before[i].dump() != after[i].dump())
				return false;
		return true;
	}

	struct Recorder {
		std::vector<AssistantEvent> Events;
		bool Idle{ false };

		size_t Count(AssistantEvent::Kind kind) const {
			return std::count_if(Events.begin(), Events.end(), [&](auto& e) { return e.Type == kind; });
		}
		const AssistantEvent* Find(AssistantEvent::Kind kind, const wchar_t* name = nullptr, const wchar_t* text = nullptr) const {
			for (auto& e : Events)
				if (e.Type == kind && (!name || e.Name == name) && (!text || e.Text.find(text) != std::wstring::npos))
					return &e;
			return nullptr;
		}
		std::wstring AllText(AssistantEvent::Kind kind) const {
			std::wstring text;
			for (auto& e : Events)
				if (e.Type == kind)
					text += e.Text;
			return text;
		}
	};
}

Task<void> AssistantSuite(DebuggerX& dbg) {
	std::unique_ptr<IDispatcher> dispatcher;
	CreateWindowDispatcher(dispatcher);

	// ---- the pieces
	{
		std::vector<std::pair<std::string, std::string>> events;
		SseParser parser;
		const std::string text = "event: a\r\ndata: {\"x\":1}\r\n\r\n: comment\n\nevent: b\ndata: line1\ndata: line2\n\ndata: plain\n\n";
		for (char c : text)
			parser.Feed(std::string_view(&c, 1), [&](auto& e, auto& d) { events.emplace_back(e, d); });
		Check(events.size() == 3 && events[0] == std::pair<std::string, std::string>("a", "{\"x\":1}")
			&& events[1].second == "line1\nline2" && events[2].first == "message", "SseParser: events in any pieces, CRLF, comments, data on several lines");

		MessageBuilder builder;
		SseParser p2;
		auto sse = Stream({ { { "type", "thinking" }, { "thinking", "I should look at the threads." }, { "signature", "sig-123" } },
			{ { "type", "text" }, { "text", "Looking." } },
			{ { "type", "tool_use" }, { "id", "toolu_1" }, { "name", "list_threads" }, { "input", { { "a", "b\"c" } } } } }, "tool_use");
		p2.Feed(sse, [&](auto& e, auto& d) { builder.Apply(e, json::parse(d)); });
		auto message = builder.Message();
		Check(builder.Complete() && message["stop_reason"] == "tool_use" && message["content"].size() == 3
			&& message["content"][0]["thinking"] == "I should look at the threads." && message["content"][0]["signature"] == "sig-123"
			&& message["content"][1]["text"] == "Looking." && message["content"][2]["input"]["a"] == "b\"c"
			&& message["usage"]["input_tokens"] == 100 && message["usage"]["output_tokens"] == 50,
			"MessageBuilder: the blocks (thinking with its signature, text, a tool call's input from its pieces), stop reason, usage");

		json content = json::array({ { { "type", "thinking" }, { "thinking", "x" } }, { { "type", "text" }, { "text", "partial" } },
			{ { "type", "tool_use" }, { "id", "t0" } }, { { "type", "fallback" }, { "from", { { "model", "a" } } }, { "to", { { "model", "b" } } } },
			{ { "type", "thinking" }, { "thinking", "y" } }, { { "type", "tool_use" }, { "id", "t1" } } });
		auto echo = EchoContent(content);
		Check(echo.size() == 4 && echo[0]["type"] == "text" && echo[1]["type"] == "fallback" && echo[2]["thinking"] == "y" && echo[3]["id"] == "t1",
			"EchoContent: before a fallback only the text and the boundary; after it, everything");
		Check(EchoContent(json::array({ { { "type", "text" } } })).size() == 1, "EchoContent: no fallback, all of it");
	}

	// ---- a conversation on procs.exe, at its DebugBreak
	auto hr = co_await dbg.LaunchAndAttach(Target(L"procs").c_str());
	Check(SUCCEEDED(hr), "launch procs.exe");
	co_await dbg.ExecuteCommand(L"g");

	Host host(dbg);
	auto transport = std::make_unique<ScriptedTransport>();
	auto& scripted = *transport;
	TransportResult overloaded;
	overloaded.Retryable = true;
	overloaded.Error = "HTTP 529: Overloaded";
	scripted.Scripts.push_back({ {}, true, overloaded });
	scripted.Scripts.push_back({ Stream({ { { "type", "thinking" }, { "thinking", "Find the worker's stack." }, { "signature", "sig-A" } },
		{ { "type", "text" }, { "text", "Let me look at the threads." } },
		{ { "type", "tool_use" }, { "id", "toolu_stacks" }, { "name", "get_all_call_stacks" }, { "input", { { "filter", "*!Worker*" } } } },
		{ { "type", "tool_use" }, { "id", "toolu_go" }, { "name", "go" }, { "input", { { "wait_seconds", 0 } } } } }, "tool_use") });
	scripted.Scripts.push_back({ Stream({ { { "type", "tool_use" }, { "id", "toolu_bad" }, { "name", "get_call_stack" }, { "input", json::object() },
		{ "raw", "{\"thread_id\": " } } }, "tool_use") });
	scripted.Scripts.push_back({ Stream({ { { "type", "text" }, { "text", "The worker waits in procs!Worker+0x62, in ReadFile." } } }, "end_turn") });
	scripted.Scripts.push_back({ Stream({}, "refusal", { { "type", "refusal" }, { "category", "cyber" } }) });

	Recorder recorder;
	int approvals = 0;
	std::wstring approvedTool;
	{
		Assistant assistant(host, std::move(transport), [&](const AssistantEvent& e) {
			recorder.Events.push_back(e);
			if (e.Type == AssistantEvent::Kind::Idle)
				recorder.Idle = true;
			}, [&](const std::wstring& tool, const std::wstring&) {
				approvals++;
				approvedTool = tool;
				return Approval::Deny;
			});
		AssistantOptions options;
		options.Model = "claude-opus-5-5";
		options.Effort = "high";
		assistant.SetOptions(options);

		Check(assistant.Send(L"What is the worker thread doing?"), "Send");
		Check(!assistant.Send(L"again"), "Send while a turn is under way: refused");
		for (int i = 0; i < 300 && !recorder.Idle; i++)
			co_await Pause{ 50, dispatcher.get() };
		Check(recorder.Idle, "the turn ends");

		for (auto& e : recorder.Events)
			if (e.Type != AssistantEvent::Kind::Text && e.Type != AssistantEvent::Kind::Thinking)
				printf("  [%d] %ls %.120ls\n", static_cast<int>(e.Type), e.Name.c_str(), e.Text.c_str());

		auto& requests = scripted.Requests;
		Check(requests.size() == 4, "four requests: an overloaded one tried again, then three rounds");
		Check(recorder.Find(AssistantEvent::Kind::Info, nullptr, L"trying again"), "the retry is shown");
		Check(recorder.AllText(AssistantEvent::Kind::Thinking) == L"Find the worker's stack." && recorder.Count(AssistantEvent::Kind::ThinkingStart) == 1,
			"the thinking summary, streamed");
		Check(recorder.AllText(AssistantEvent::Kind::Text) == L"Let me look at the threads.The worker waits in procs!Worker+0x62, in ReadFile.",
			"the text, streamed");
		auto stacks = recorder.Find(AssistantEvent::Kind::ToolResult, L"get_all_call_stacks");
		Check(stacks && !stacks->Failed && stacks->Text.find(L"procs!Worker") != std::wstring::npos, "get_all_call_stacks ran on the target");
		Check(approvals == 1 && approvedTool == L"go", "go (it runs the target) is asked for; the read-only tools are not");
		auto go = recorder.Find(AssistantEvent::Kind::ToolResult, L"go");
		Check(go && go->Failed && g_Events.Status == ExecutionStatus::Break, "denied: not run (the target stays stopped)");
		auto bad = recorder.Find(AssistantEvent::Kind::ToolResult, L"get_call_stack");
		Check(bad && bad->Failed, "a call whose input is not valid JSON is not run");
		auto usage = std::find_if(recorder.Events.rbegin(), recorder.Events.rend(), [](auto& e) { return e.Type == AssistantEvent::Kind::Usage; });
		Check(usage != recorder.Events.rend() && usage->InputTokens == 300 && usage->OutputTokens == 150 && usage->CacheReadTokens == 120, "usage, added up");

		if (requests.size() == 4) {
			auto& first = requests[1];
			Check(first["model"] == "claude-opus-5-5" && first["stream"] == true && first["output_config"]["effort"] == "high"
				&& first["thinking"]["type"] == "adaptive" && first["cache_control"]["type"] == "ephemeral" && first["fallbacks"] == "default"
				&& scripted.Betas[1] == std::vector<std::string>{ "server-side-fallback-2026-07-01" }, "the request: model, effort, adaptive thinking, caching, fallbacks");
			Check(first["tools"].size() == DebugX::Mcp::McpServer::ToolNames().size() && first["tools"][0].value("eager_input_streaming", false),
				"every tool declared, inputs streamed eagerly");
			auto state = first["messages"][0]["content"][0].value("text", "");
			Check(state.find("<debugger_state>") == 0 && state.find("procs.exe") != std::string::npos && state.find("stopped") != std::string::npos,
				"the user's message begins with the debugger's state");
			bool same = true, prefix = true;
			for (size_t i = 1; i < requests.size(); i++) {
				same = same && requests[i]["system"] == requests[0]["system"] && requests[i]["tools"] == requests[0]["tools"];
				prefix = prefix && IsPrefix(requests[i - 1]["messages"], requests[i]["messages"]);
			}
			Check(same && prefix, "append-only: the system prompt and tools never change, earlier messages are sent again byte for byte");

			auto& second = requests[2]["messages"];
			Check(second.size() == 3 && second[1]["role"] == "assistant" && second[1]["content"][0]["signature"] == "sig-A"
				&& second[1]["content"][0]["thinking"] == "Find the worker's stack.", "the answer goes back as it came (the thinking block with its signature)");
			auto& results = second[2]["content"];
			Check(results.size() == 2 && results[0]["tool_use_id"] == "toolu_stacks" && results[0]["is_error"] == false
				&& results[0]["content"].get<std::string>().find("procs!Worker") != std::string::npos
				&& results[1]["tool_use_id"] == "toolu_go" && results[1]["is_error"] == true, "both results, in one message, by id");
			auto& third = requests[3]["messages"];
			Check(third.back()["content"][0]["is_error"] == true && third.back()["content"][0]["content"].get<std::string>().find("INVALID_JSON") != std::string::npos,
				"the invalid input is answered as such");
		}
		Check(assistant.Messages().size() == 6, "the conversation: 3 rounds, 6 messages");

		// a refusal: shown, and not part of the conversation
		recorder.Idle = false;
		assistant.Send(L"Something it declines");
		for (int i = 0; i < 100 && !recorder.Idle; i++)
			co_await Pause{ 50, dispatcher.get() };
		Check(recorder.Find(AssistantEvent::Kind::Error, nullptr, L"declined") && assistant.Messages().size() == 7
			&& assistant.Messages().back()["role"] == "user", "a refusal: said so; the answer is left out");
		Check(assistant.Reset() && assistant.Messages().empty(), "Reset: a new conversation");
	}

	// ---- the real API, if asked for (it is billed)
	wchar_t key[512]{};
	if (::GetEnvironmentVariableW(L"DEBUGENGINETESTS_LIVE_API", nullptr, 0) && ::GetEnvironmentVariableW(L"ANTHROPIC_API_KEY", key, _countof(key))) {
		Recorder live;
		{
			Assistant assistant(host, CreateHttpTransport(std::string(CW2A(key))), [&](const AssistantEvent& e) {
				live.Events.push_back(e);
				if (e.Type == AssistantEvent::Kind::Idle)
					live.Idle = true;
				}, [](auto&, auto&) { return Approval::Deny; });
			AssistantOptions options;
			options.Effort = "low";
			assistant.SetOptions(options);
			assistant.Send(L"How many threads does the target have, and what is each one's name? Answer in one sentence.");
			for (int i = 0; i < 2400 && !live.Idle; i++)
				co_await Pause{ 50, dispatcher.get() };
		}
		for (auto& e : live.Events)
			if (e.Type == AssistantEvent::Kind::ToolCall || e.Type == AssistantEvent::Kind::Error || e.Type == AssistantEvent::Kind::Info)
				printf("  live [%d] %ls %.200ls\n", static_cast<int>(e.Type), e.Name.c_str(), e.Text.c_str());
		printf("  live answer: %ls\n", live.AllText(AssistantEvent::Kind::Text).c_str());
		Check(live.Idle && !live.Find(AssistantEvent::Kind::Error) && live.Count(AssistantEvent::Kind::ToolCall) > 0
			&& live.AllText(AssistantEvent::Kind::Text).find(L"Worker") != std::wstring::npos, "the real API: it used a tool, and names the worker");
	}
	else
		printf("SKIP: the real API (DEBUGENGINETESTS_LIVE_API=1 and ANTHROPIC_API_KEY run it; it is billed)\n");

	co_await dbg.Stop();
	::PostQuitMessage(0);
}
