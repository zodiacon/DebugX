// The MCP server (DebugX.Mcp): its HTTP transport and JSON-RPC, and its tools on a live target (procs.exe), driven as a
// client drives it -- over HTTP, from another thread, while the tools run on this one's message loop
#include "Harness.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <coroutine>
#include <functional>
#include <map>
#include <thread>
#include <nlohmann/json.hpp>
#include "McpServer.h"

#pragma comment(lib, "ws2_32.lib")

using namespace DebugEngine;
using json = nlohmann::json;

namespace {
	class TestHost final : public DebugX::Mcp::IMcpHost {
	public:
		explicit TestHost(DebuggerX& dbg) : m_Dbg(dbg) {}
		IDebugger& Debugger() override {
			return m_Dbg;
		}
		ExecutionStatus Status() override {
			return g_Events.Status;
		}
		std::wstring TargetName() override {
			return L"procs.exe";
		}
		void ToolCalled(const std::wstring& tool, const std::wstring& arguments, bool failed) override {
			Calls++;
		}
		int Calls{ 0 };

	private:
		DebuggerX& m_Dbg;
	};

	// Runs 'work' on a thread of its own; the coroutine goes on (on this thread, through the dispatcher) when it is done --
	// meanwhile this thread's message loop runs the tools the work asks the server for
	struct OnThread {
		std::function<void()> Work;
		IDispatcher* Dispatcher;
		bool await_ready() const noexcept {
			return false;
		}
		void await_suspend(std::coroutine_handle<> handle) {
			std::thread([work = Work, dispatcher = Dispatcher, handle] {
				work();
				dispatcher->Post([handle] { handle.resume(); });
				}).detach();
		}
		void await_resume() const noexcept {}
	};

	struct HttpResult {
		int Status{ 0 };
		std::string Body;
	};

	// one request on a connection of its own (blocking: a worker thread's)
	HttpResult Http(unsigned short port, const std::string& method, const std::string& path, const std::string& headers, const std::string& body) {
		HttpResult result;
		SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_port = ::htons(port);
		address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
		if (::connect(s, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
			::closesocket(s);
			return result;
		}
		auto request = std::format("{} {} HTTP/1.1\r\n{}Content-Type: application/json\r\nAccept: application/json, text/event-stream\r\n"
			"Content-Length: {}\r\nConnection: close\r\n\r\n{}", method, path, headers, body.size(), body);
		::send(s, request.data(), static_cast<int>(request.size()), 0);
		std::string response;
		char chunk[16384];
		int n;
		while ((n = ::recv(s, chunk, sizeof(chunk), 0)) > 0)
			response.append(chunk, n);
		::closesocket(s);
		if (response.size() > 12)
			result.Status = atoi(response.c_str() + 9);
		if (auto end = response.find("\r\n\r\n"); end != std::string::npos)
			result.Body = response.substr(end + 4);
		return result;
	}

	struct Client {
		unsigned short Port;
		std::string Token;
		int NextId{ 1 };

		std::string Headers() const {
			return "Host: 127.0.0.1:" + std::to_string(Port) + "\r\nAuthorization: Bearer " + Token + "\r\n";
		}
		HttpResult Post(const json& message) {
			return Http(Port, "POST", "/mcp", Headers(), message.dump());
		}
		json Rpc(const char* method, json params = json::object()) {
			auto r = Post({ { "jsonrpc", "2.0" }, { "id", NextId++ }, { "method", method }, { "params", params } });
			auto parsed = json::parse(r.Body, nullptr, false);
			return parsed.is_discarded() ? json() : parsed;
		}
		// a tool's text; 'error' says whether the tool said it failed
		std::string Tool(const char* name, json args, bool* error = nullptr) {
			auto r = Rpc("tools/call", { { "name", name }, { "arguments", args } });
			if (!r.contains("result")) {
				if (error)
					*error = true;
				return r.dump();
			}
			if (error)
				*error = r["result"].value("isError", false);
			auto& content = r["result"]["content"];
			return content.is_array() && !content.empty() ? content[0].value("text", "") : std::string();
		}
	};

	bool Contains(const std::string& text, const char* part) {
		return text.find(part) != std::string::npos;
	}

	void Show(const char* title, const std::string& text) {
		printf("  --- %s:\n", title);
		size_t lines = 0;
		for (size_t pos = 0; pos < text.size() && lines < 25; lines++) {
			auto end = text.find('\n', pos);
			if (end == std::string::npos)
				end = text.size();
			printf("    %s\n", text.substr(pos, end - pos).c_str());
			pos = end + 1;
		}
	}
}

Task<void> McpSuite(DebuggerX& dbg) {
	std::unique_ptr<IDispatcher> dispatcher;
	CreateWindowDispatcher(dispatcher);
	TestHost host(dbg);
	DebugX::Mcp::McpServer server(host);
	auto token = DebugX::Mcp::McpServer::NewToken();
	auto hr = server.Start(0, token);
	Check(SUCCEEDED(hr) && server.Port() != 0 && token.size() == 64, "the server listens (on a free port), with a random token");
	printf("  %ls\n", server.Url().c_str());
	Client client{ server.Port(), token };
	const auto port = server.Port();

	// ---- the transport, before there is a target
	HttpResult noToken, badToken, badHost, badOrigin, get, notFound, accepted;
	json init, list, batch;
	std::string session;
	bool sessionError = false, threadsError = false;
	co_await OnThread{ [&] {
		const std::string body = R"({"jsonrpc":"2.0","id":1,"method":"ping"})";
		const std::string host = "Host: 127.0.0.1:" + std::to_string(port) + "\r\n";
		noToken = Http(port, "POST", "/mcp", host, body);
		badToken = Http(port, "POST", "/mcp", host + "Authorization: Bearer 1234\r\n", body);
		badHost = Http(port, "POST", "/mcp", "Host: evil.example.com\r\nAuthorization: Bearer " + token + "\r\n", body);
		badOrigin = Http(port, "POST", "/mcp", client.Headers() + "Origin: https://evil.example.com\r\n", body);
		get = Http(port, "GET", "/mcp", client.Headers(), "");
		notFound = Http(port, "POST", "/other", client.Headers(), body);
		init = client.Rpc("initialize", { { "protocolVersion", "2025-06-18" }, { "capabilities", json::object() },
			{ "clientInfo", { { "name", "DebugEngineTests" }, { "version", "1" } } } });
		accepted = client.Post({ { "jsonrpc", "2.0" }, { "method", "notifications/initialized" } });
		list = client.Rpc("tools/list");
		auto b = client.Post(json::array({ { { "jsonrpc", "2.0" }, { "id", 100 }, { "method", "ping" } },
			{ { "jsonrpc", "2.0" }, { "method", "notifications/initialized" } }, { { "jsonrpc", "2.0" }, { "id", 101 }, { "method", "nope" } } }));
		batch = json::parse(b.Body, nullptr, false);
		session = client.Tool("get_session", json::object(), &sessionError);
		client.Tool("list_threads", json::object(), &threadsError);
		}, dispatcher.get() };
	Check(noToken.Status == 401 && badToken.Status == 401, "no token, or a wrong one: 401");
	Check(badHost.Status == 403 && badOrigin.Status == 403, "a Host or Origin that is not this computer's: 403 (DNS rebinding)");
	Check(get.Status == 405 && notFound.Status == 404, "GET: 405 (no event stream); another path: 404");
	Check(init.contains("result") && init["result"].value("protocolVersion", "") == "2025-06-18"
		&& init["result"]["serverInfo"].value("name", "") == "debugx" && !init["result"].value("instructions", "").empty()
		&& init["result"]["capabilities"].contains("tools"), "initialize: the client's protocol version, server info, instructions, tools");
	Check(accepted.Status == 202 && accepted.Body.empty(), "a notification: 202, nothing to answer");
	bool allListed = list.contains("result");
	if (allListed) {
		for (auto& name : DebugX::Mcp::McpServer::ToolNames())
			allListed = allListed && std::any_of(list["result"]["tools"].begin(), list["result"]["tools"].end(), [&](auto& t) {
				return t.value("name", "") == name && t["inputSchema"].value("type", "") == "object" && t.contains("annotations");
			});
		printf("  %zu tools\n", list["result"]["tools"].size());
	}
	Check(allListed, "tools/list: every tool, with its input schema and annotations");
	Check(batch.is_array() && batch.size() == 2 && batch[0]["id"] == 100 && batch[1]["error"]["code"] == -32601,
		"a batch: an answer per request (none for the notification); an unknown method: -32601");
	Check(!sessionError && Contains(session, "No target"), "get_session without a target says so");
	Check(threadsError, "list_threads without a target: an error result");

	// ---- a target: procs.exe at its DebugBreak (a main thread and a worker, named)
	hr = co_await dbg.LaunchAndAttach(Target(L"procs").c_str());
	Check(SUCCEEDED(hr), "launch procs.exe");
	co_await dbg.ExecuteCommand(L"g");
	std::vector<ProcessInfo> processes;
	std::vector<ThreadInfo> threads;
	co_await dbg.EnumProcesses(processes, threads);
	auto worker = std::find_if(threads.begin(), threads.end(), [](auto& t) { return t.Name == L"Worker"; });
	std::vector<StackFrameInfo> frames;
	co_await dbg.GetStackTrace(frames);
	auto wmain = std::find_if(frames.begin(), frames.end(), [](auto& f) { return f.Symbol.starts_with(L"procs!wmain"); });
	Check(worker != threads.end() && wmain != frames.end(), "(the worker thread, and the main thread's wmain frame)");
	const ULONG workerId = worker != threads.end() ? worker->Id : 1;
	const ULONG wmainFrame = wmain != frames.end() ? wmain->Index : 1;
	wchar_t source[MAX_PATH];
	::GetFullPathNameW((Target(L"procs") + L"\\..\\..\\..\\..\\Tests\\Targets\\procs\\procs.cpp").c_str(), _countof(source), source, nullptr);
	const std::string sourcePath(CW2A(source, CP_UTF8));

	struct Result {
		std::string Text;
		bool Error{ false };
	};
	std::map<std::string, Result> r;
	const int contextBefore = g_Events.ContextChanged;
	int contextAfterStacks = 0;
	co_await OnThread{ [&] {
		auto call = [&](const char* key, const char* tool, json args) {
			Result result;
			result.Text = client.Tool(tool, std::move(args), &result.Error);
			r[key] = result;
		};
		call("session", "get_session", json::object());
		call("threads", "list_threads", json::object());
		// (two at once, from two connections: they take their turns)
		Result stacks;
		std::thread other([&] {
			Client c2{ port, token };
			stacks.Text = c2.Tool("get_all_call_stacks", json::object(), &stacks.Error);
			});
		call("filtered", "get_all_call_stacks", { { "filter", "*!Worker*" } });
		other.join();
		r["stacks"] = stacks;
		call("stack", "get_call_stack", { { "thread_id", workerId } });
		call("badthread", "get_call_stack", { { "thread_id", 999 } });
		call("eval", "evaluate", { { "expressions", { "procs!g_Ready", "1 + 2" } } });
		call("evalbad", "evaluate", { { "expressions", { "no_such_variable_here" } } });
		call("memory", "read_memory", { { "address", "procs!g_Ready" }, { "size", 4 }, { "format", "dwords" } });
		call("bytes", "read_memory", { { "address", "procs!g_Ready" }, { "size", 32 } });
		call("modules", "list_modules", { { "filter", "procs" } });
		call("disasm", "disassemble", { { "count", 4 } });
		call("registers", "get_registers", json::object());
		call("locals", "get_locals", { { "frame", wmainFrame }, { "depth", 0 } });
		call("source", "get_source", { { "address", "procs!wmain" }, { "context", 20 } });
		call("sourcebyline", "get_source", { { "file", sourcePath }, { "line", 34 }, { "context", 1 } });
		call("sourceany", "get_source", { { "file", "C:\\Windows\\win.ini" }, { "line", 1 } });
		call("command", "run_command", { { "command", ".echo hello from mcp" } });
		call("badcommand", "run_command", { { "command", "thisisnotacommand" } });
		call("search", "search_memory", { { "pattern", "L\"DebugXTestEvent\"" }, { "start", "procs" }, { "max_results", 2 } });
		call("handles", "list_handles", { { "filter", "DebugXTestEvent" } });
		call("badargs", "read_memory", { { "address", "procs!g_Ready" }, { "size", "lots" } });
		call("unknown", "no_such_tool", json::object());
		}, dispatcher.get() };
	contextAfterStacks = g_Events.ContextChanged;
	for (auto& [key, result] : r)
		Show((key + (result.Error ? " (error)" : "")).c_str(), result.Text);

	Check(!r["session"].Error && Contains(r["session"].Text, "stopped") && Contains(r["session"].Text, "procs") && Contains(r["session"].Text, "Current thread"),
		"get_session: stopped, the process, the current thread");
	Check(Contains(r["threads"].Text, "\"Main Thread\"") && Contains(r["threads"].Text, "\"Worker\""), "list_threads: both, by name");
	Check(!r["stacks"].Error && Contains(r["stacks"].Text, "distinct stack") && Contains(r["stacks"].Text, "procs!wmain"), "get_all_call_stacks (at the same time as another call)");
	Check(Contains(r["filtered"].Text, "1 with a frame matching") && Contains(r["filtered"].Text, "> #") && Contains(r["filtered"].Text, "procs!Worker")
		&& !Contains(r["filtered"].Text, "procs!wmain"), "get_all_call_stacks with a filter: only the worker, its frame marked");
	Check(contextAfterStacks == contextBefore, "walking all the stacks does not change the current thread (no ContextChanged)");
	Check(!r["stack"].Error && Contains(r["stack"].Text, "ReadFile") && Contains(r["stack"].Text, "procs!Worker"), "get_call_stack of the worker: in ReadFile, from Worker");
	Check(r["badthread"].Error, "get_call_stack of a thread there is not: an error result");
	Check(!r["eval"].Error && Contains(r["eval"].Text, "procs!g_Ready = 1") && Contains(r["eval"].Text, "= 3"), "evaluate: a global (1) and arithmetic");
	Check(Contains(r["evalbad"].Text, "[error]"), "evaluate: what does not evaluate says so");
	Check(Contains(r["memory"].Text, " 00000001"), "read_memory: the global as a dword");
	Check(Contains(r["bytes"].Text, " 01 00 00 00"), "read_memory: bytes");
	Check(Contains(r["modules"].Text, "procs ") && Contains(r["modules"].Text, "procs.exe"), "list_modules with a filter");
	Check(Contains(r["disasm"].Text, "=> "), "disassemble: the current instruction marked");
	Check(Contains(r["registers"].Text, "rip=") && !Contains(r["registers"].Text, "xmm0="), "get_registers: rip, no vector registers by default");
	Check(!r["locals"].Error && Contains(r["locals"].Text, "path") && Contains(r["locals"].Text, "commandLine"), "get_locals of wmain's frame");
	Check(!r["source"].Error && Contains(r["source"].Text, "DebugBreak") && Contains(r["source"].Text, "procs.cpp"), "get_source of an address");
	Check(!r["sourcebyline"].Error && Contains(r["sourcebyline"].Text, ">   34") && Contains(r["sourcebyline"].Text, "::DebugBreak();"), "get_source of a file and line");
	Check(r["sourceany"].Error && !Contains(r["sourceany"].Text, "[fonts]"), "get_source of a file the symbols do not know: refused (not any file of the machine)");
	Check(!r["command"].Error && Contains(r["command"].Text, "hello from mcp"), "run_command: its output");
	Check(!r["badcommand"].Error && !r["badcommand"].Text.empty() && r["badcommand"].Text != "(no output)", "run_command: what the engine says of a bad command");
	Check(Contains(r["search"].Text, "Found"), "search_memory: UTF-16 text");
	Check(Contains(r["handles"].Text, "1 of") || Contains(r["handles"].Text, "DebugXTestEvent"), "list_handles with a filter");
	Check(r["badargs"].Error && Contains(r["badargs"].Text, "arguments"), "an argument of the wrong type: an error result");
	Check(Contains(r["unknown"].Text, "-32602"), "an unknown tool: -32602");

	// the scope frame is the user's again after get_locals went to another
	ULONG64 ip = 0;
	ULONG frame = 99;
	co_await dbg.GetScope(ip, frame);
	Check(frame == 0, "the scope frame is put back after get_locals of another frame");

	// ---- running: a breakpoint on a source line, go (not waiting), wait_for_stop, step, break_into on a stopped target
	co_await OnThread{ [&] {
		auto call = [&](const char* key, const char* tool, json args) {
			Result result;
			result.Text = client.Tool(tool, std::move(args), &result.Error);
			r[key] = result;
		};
		call("setbp", "set_breakpoint", { { "file", sourcePath }, { "line", 48 } });
		call("bps", "list_breakpoints", json::object());
		call("go", "go", { { "wait_seconds", 0 } });
		call("wait", "wait_for_stop", { { "timeout_seconds", 60 } });
		call("step", "step", { { "kind", "over" } });
		call("break", "break_into", json::object());
		}, dispatcher.get() };
	for (auto key : { "setbp", "bps", "go", "wait", "step", "break" })
		Show((std::string(key) + (r[key].Error ? " (error)" : "")).c_str(), r[key].Text);
	Check(!r["setbp"].Error && Contains(r["setbp"].Text, "Set: ["), "set_breakpoint on a file and line");
	Check(Contains(r["bps"].Text, "procs.cpp:48"), "list_breakpoints: it");
	Check(!r["go"].Error && Contains(r["go"].Text, "still running"), "go, not waiting: the target is running (not an error)");
	Check(!r["wait"].Error && Contains(r["wait"].Text, "Status: stopped") && Contains(r["wait"].Text, "procs.cpp:48"), "wait_for_stop: stopped at the breakpoint, and says where");
	Check(!r["step"].Error && Contains(r["step"].Text, "procs.cpp:49"), "step over: the next line");
	Check(!r["break"].Error && Contains(r["break"].Text, "already stopped"), "break_into a stopped target");
	Check(host.Calls > 20, "the host is told of each call (its log)");

	co_await dbg.Stop();
	server.Stop();
	Check(!server.IsRunning(), "Stop");
	::PostQuitMessage(0);
}
