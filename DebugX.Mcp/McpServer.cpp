#include "pch.h"
#include "McpServer.h"
#include "McpTools.h"
#include "Dispatcher.h"
#include <algorithm>
#include <deque>
#include <map>
#include <set>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")

using namespace DebugEngine;

namespace DebugX::Mcp {

namespace {
	// the protocol versions the server speaks (it answers with the client's, if it is one of them)
	const char* const ProtocolVersions[] = { "2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05" };

	constexpr size_t MaxHeaderBytes = 64 * 1024;
	constexpr size_t MaxBodyBytes = 8 * 1024 * 1024;

	// What the connections' threads, the server and the tools' completions share (kept alive by whoever still uses it)
	struct Shared {
		std::mutex Lock;
		std::condition_variable Changed;
		bool Stopping{ false };
		std::set<SOCKET> Clients;
		int Threads{ 0 };
	};

	// a tool's result, once the tool has run (under Shared::Lock)
	struct Call {
		bool Done{ false };
		ToolResult Result;
	};

	// The UI thread's side: the serialized tools run one after the other (a tool may switch the current thread or frame,
	// and switch it back, across its co_awaits)
	struct UiState {
		explicit UiState(IMcpHost& host) : Host(host) {}

		void Add(std::function<void()> start) {
			Waiting.push_back(std::move(start));
			if (!Running)
				Next();
		}
		void Next() {
			if (Waiting.empty()) {
				Running = false;
				return;
			}
			Running = true;
			auto start = std::move(Waiting.front());
			Waiting.pop_front();
			start();
		}

		ToolContext Context() {
			return { Host, Host.Debugger(), &RunsInFlight };
		}

		IMcpHost& Host;
		std::deque<std::function<void()>> Waiting;
		bool Running{ false };
		int RunsInFlight{ 0 };		// (ToolContext::RunsInFlight)
	};

	using ToolFunction = Task<ToolResult>(*)(ToolContext, json);

	Task<void> RunTool(std::shared_ptr<UiState> ui, std::shared_ptr<Shared> shared, std::shared_ptr<Call> call, std::string name,
		ToolFunction function, json args, bool serialized, bool log) {
		auto context = ui->Context();
		ToolResult result;
		try {
			result = co_await function(context, args);
		}
		catch (const std::exception& e) {
			result = { std::string("The arguments are not right: ") + e.what(), true };
		}
		if (log)
			ui->Host.ToolCalled(FromUtf8(name), FromUtf8(args.dump(-1, ' ', false, json::error_handler_t::replace)), result.IsError);
		{
			std::lock_guard lock(shared->Lock);
			call->Result = std::move(result);
			call->Done = true;
		}
		shared->Changed.notify_all();
		if (serialized)
			ui->Next();
	}

	struct HttpRequest {
		std::string Method, Path, Version;
		std::map<std::string, std::string> Headers;		// (names in lowercase)
		std::string Body;

		std::string Header(const char* name) const {
			auto it = Headers.find(name);
			return it == Headers.end() ? std::string() : it->second;
		}
	};

	struct HttpResponse {
		int Status{ 200 };
		std::string Body;
		std::string ContentType{ "application/json" };
		std::vector<std::string> Headers;
	};

	std::string LowerA(std::string s) {
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
		return s;
	}

	std::string Trim(std::string_view s) {
		while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
			s.remove_prefix(1);
		while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
			s.remove_suffix(1);
		return std::string(s);
	}

	const char* Reason(int status) {
		switch (status) {
			case 200: return "OK";
			case 202: return "Accepted";
			case 400: return "Bad Request";
			case 401: return "Unauthorized";
			case 403: return "Forbidden";
			case 404: return "Not Found";
			case 405: return "Method Not Allowed";
			case 413: return "Payload Too Large";
			default: return "Error";
		}
	}

	bool SendAll(SOCKET s, const std::string& data) {
		size_t sent = 0;
		while (sent < data.size()) {
			int n = ::send(s, data.data() + sent, static_cast<int>(std::min<size_t>(data.size() - sent, 1 << 20)), 0);
			if (n <= 0)
				return false;
			sent += n;
		}
		return true;
	}

	// more bytes into the buffer; false: the connection is closed (or broken)
	bool Receive(SOCKET s, std::string& buffer) {
		char chunk[16384];
		int n = ::recv(s, chunk, sizeof(chunk), 0);
		if (n <= 0)
			return false;
		buffer.append(chunk, n);
		return true;
	}

	bool ConstantTimeEquals(const std::string& a, const std::string& b) {
		if (a.size() != b.size())
			return false;
		unsigned char diff = 0;
		for (size_t i = 0; i < a.size(); i++)
			diff |= static_cast<unsigned char>(a[i] ^ b[i]);
		return diff == 0;
	}

	// a host (Host header, Origin's) that is this computer's loopback address, with or without a port
	bool IsLoopbackHost(std::string host) {
		host = LowerA(host);
		for (std::string_view name : { "[::1]", "127.0.0.1", "localhost" }) {
			if (host.starts_with(name)) {
				auto port = host.substr(name.size());
				return port.empty() || (port[0] == ':' && port.size() > 1 && port.find_first_not_of("0123456789", 1) == std::string::npos);
			}
		}
		return false;
	}

	json RpcError(const json& id, int code, const std::string& message) {
		return { { "jsonrpc", "2.0" }, { "id", id }, { "error", { { "code", code }, { "message", message } } } };
	}

	json RpcResult(const json& id, json result) {
		return { { "jsonrpc", "2.0" }, { "id", id }, { "result", std::move(result) } };
	}
}

struct McpServer::Impl {
	explicit Impl(IMcpHost& host) : Host(host) {}

	IMcpHost& Host;
	std::unique_ptr<IDispatcher> Dispatcher;
	std::shared_ptr<Shared> State;
	std::shared_ptr<UiState> Ui;
	SOCKET Listener{ INVALID_SOCKET };
	std::thread AcceptThread;
	std::string Token;
	unsigned short Port{ 0 };
	bool Running{ false };
	bool WsaStarted{ false };

	void AcceptLoop();
	void Serve(SOCKET s);
	// reads one request (what is past it stays in 'buffer'); false: none (closed, or malformed: 'status' then says how to answer)
	bool ReadRequest(SOCKET s, std::string& buffer, HttpRequest& request, int& status);
	HttpResponse Handle(const HttpRequest& request);
	// a JSON-RPC message; null: nothing to answer (a notification, a response)
	json HandleMessage(const json& message);
	json CallTool(const ToolDef& tool, json args);

	// Runs a tool's function on the UI thread and waits for its result, up to 'timeoutSeconds'; none: it timed out (or the
	// server is stopping: 'stopping' then says so)
	std::optional<ToolResult> Invoke(const std::string& name, ToolFunction function, json args, bool serialized, bool log, int timeoutSeconds, bool& stopping);
	// what the target is doing, asked on the UI thread: 0 running, 1 stopped, 2 no target; -1: the server is stopping
	int TargetState();
	// waits on Shared::Changed for up to 'ms'; false: the server is stopping
	bool Pause(int ms);
};

McpServer::McpServer(IMcpHost& host) : m_Impl(std::make_unique<Impl>(host)) {}

McpServer::~McpServer() {
	Stop();
}

HRESULT McpServer::Start(unsigned short port, std::string token) {
	Stop();
	auto& m = *m_Impl;
	WSADATA wsa;
	if (int error = ::WSAStartup(MAKEWORD(2, 2), &wsa))
		return HRESULT_FROM_WIN32(error);
	m.WsaStarted = true;
	auto fail = [&](int error) {
		if (m.Listener != INVALID_SOCKET)
			::closesocket(std::exchange(m.Listener, INVALID_SOCKET));
		m.Dispatcher.reset();
		::WSACleanup();
		m.WsaStarted = false;
		return HRESULT_FROM_WIN32(error);
	};

	auto hr = CreateWindowDispatcher(m.Dispatcher);
	if (FAILED(hr)) {
		fail(0);
		return hr;
	}
	m.Listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (m.Listener == INVALID_SOCKET)
		return fail(::WSAGetLastError());
	// (nobody else gets the port while the server has it, not even with SO_REUSEADDR)
	BOOL exclusive = TRUE;
	::setsockopt(m.Listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_port = ::htons(port);
	address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
	if (::bind(m.Listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR || ::listen(m.Listener, SOMAXCONN) == SOCKET_ERROR)
		return fail(::WSAGetLastError());
	int length = sizeof(address);
	::getsockname(m.Listener, reinterpret_cast<sockaddr*>(&address), &length);
	m.Port = ::ntohs(address.sin_port);
	m.Token = std::move(token);
	m.State = std::make_shared<Shared>();
	m.Ui = std::make_shared<UiState>(m.Host);
	m.Running = true;
	m.AcceptThread = std::thread([&m] { m.AcceptLoop(); });
	return S_OK;
}

void McpServer::Stop() {
	auto& m = *m_Impl;
	if (!m.Running)
		return;
	{
		std::lock_guard lock(m.State->Lock);
		m.State->Stopping = true;
		for (auto s : m.State->Clients) {
			::shutdown(s, SD_BOTH);
			::closesocket(s);
		}
		m.State->Clients.clear();
	}
	m.State->Changed.notify_all();
	::closesocket(std::exchange(m.Listener, INVALID_SOCKET));
	m.AcceptThread.join();
	{
		std::unique_lock lock(m.State->Lock);
		m.State->Changed.wait(lock, [&] { return m.State->Threads == 0; });
	}
	// (what is still queued for the UI thread is dropped; a tool already running in the debugger finishes on its own, and its
	// result goes nowhere)
	m.Dispatcher.reset();
	m.Ui.reset();
	m.State.reset();
	::WSACleanup();
	m.WsaStarted = false;
	m.Running = false;
}

bool McpServer::IsRunning() const {
	return m_Impl->Running;
}

unsigned short McpServer::Port() const {
	return m_Impl->Port;
}

std::wstring McpServer::Url() const {
	return std::format(L"http://127.0.0.1:{}/mcp", m_Impl->Port);
}

std::string McpServer::NewToken() {
	BYTE bytes[32];
	if (!BCRYPT_SUCCESS(::BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
		return {};
	std::string token;
	for (auto b : bytes)
		token += std::format("{:02x}", b);
	return token;
}

std::vector<std::string> McpServer::ToolNames() {
	std::vector<std::string> names;
	for (auto& t : Tools())
		names.push_back(t.Name);
	return names;
}

void McpServer::Impl::AcceptLoop() {
	auto shared = State;
	const auto listener = Listener;		// (Stop closes it, which ends accept)
	while (true) {
		SOCKET s = ::accept(listener, nullptr, nullptr);
		if (s == INVALID_SOCKET) {
			std::lock_guard lock(shared->Lock);
			if (shared->Stopping)
				return;
			continue;
		}
		BOOL noDelay = TRUE;
		::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
		{
			std::lock_guard lock(shared->Lock);
			if (shared->Stopping) {
				::closesocket(s);
				return;
			}
			shared->Clients.insert(s);
			shared->Threads++;
		}
		std::thread([this, s, shared] {
			Serve(s);
			{
				std::lock_guard lock(shared->Lock);
				// (closed here, unless Stop has closed it already)
				if (shared->Clients.erase(s))
					::closesocket(s);
				shared->Threads--;
			}
			shared->Changed.notify_all();
			}).detach();
	}
}

void McpServer::Impl::Serve(SOCKET s) {
	std::string buffer;
	while (true) {
		HttpRequest request;
		int status = 0;
		if (!ReadRequest(s, buffer, request, status)) {
			if (status) {
				HttpResponse response{ status, std::format("{{\"error\":\"{}\"}}", Reason(status)) };
				SendAll(s, std::format("HTTP/1.1 {} {}\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}",
					status, Reason(status), response.Body.size(), response.Body));
			}
			return;
		}
		auto response = Handle(request);
		{
			std::lock_guard lock(State->Lock);
			if (State->Stopping)
				return;
		}
		auto connection = LowerA(request.Header("connection"));
		const bool close = connection == "close" || (request.Version == "HTTP/1.0" && connection != "keep-alive");
		std::string text = std::format("HTTP/1.1 {} {}\r\n", response.Status, Reason(response.Status));
		if (!response.Body.empty())
			text += "Content-Type: " + response.ContentType + "\r\n";
		text += std::format("Content-Length: {}\r\n", response.Body.size());
		for (auto& h : response.Headers)
			text += h + "\r\n";
		text += close ? "Connection: close\r\n" : "Connection: keep-alive\r\n";
		text += "\r\n" + response.Body;
		if (!SendAll(s, text) || close)
			return;
	}
}

bool McpServer::Impl::ReadRequest(SOCKET s, std::string& buffer, HttpRequest& request, int& status) {
	size_t end;
	while ((end = buffer.find("\r\n\r\n")) == std::string::npos) {
		if (buffer.size() > MaxHeaderBytes) {
			status = 413;
			return false;
		}
		if (!Receive(s, buffer))
			return false;
	}
	std::string_view head(buffer.data(), end);
	auto lineEnd = head.find("\r\n");
	auto requestLine = head.substr(0, lineEnd);
	auto sp1 = requestLine.find(' '), sp2 = requestLine.rfind(' ');
	if (sp1 == std::string_view::npos || sp2 == sp1) {
		status = 400;
		return false;
	}
	request.Method = requestLine.substr(0, sp1);
	request.Path = requestLine.substr(sp1 + 1, sp2 - sp1 - 1);
	request.Version = requestLine.substr(sp2 + 1);
	for (size_t pos = lineEnd == std::string_view::npos ? head.size() : lineEnd + 2; pos < head.size(); ) {
		auto next = head.find("\r\n", pos);
		if (next == std::string_view::npos)
			next = head.size();
		auto line = head.substr(pos, next - pos);
		if (auto colon = line.find(':'); colon != std::string_view::npos)
			request.Headers[LowerA(Trim(line.substr(0, colon)))] = Trim(line.substr(colon + 1));
		pos = next + 2;
	}
	buffer.erase(0, end + 4);

	if (LowerA(request.Header("transfer-encoding")).find("chunked") != std::string::npos) {
		while (true) {
			size_t lineEnd2;
			while ((lineEnd2 = buffer.find("\r\n")) == std::string::npos)
				if (!Receive(s, buffer))
					return false;
			// (checked before it is a size_t, which is 32 bits in an x86 build, and before it is added to anything)
			const auto chunk = std::strtoull(buffer.substr(0, lineEnd2).c_str(), nullptr, 16);
			if (chunk > MaxBodyBytes || request.Body.size() + chunk > MaxBodyBytes) {
				status = 413;
				return false;
			}
			const auto size = static_cast<size_t>(chunk);
			while (buffer.size() < lineEnd2 + 2 + size + 2)
				if (!Receive(s, buffer))
					return false;
			request.Body.append(buffer, lineEnd2 + 2, size);
			buffer.erase(0, lineEnd2 + 2 + size + 2);
			if (size == 0)
				break;
		}
		return true;
	}
	const auto contentLength = std::strtoull(request.Header("content-length").c_str(), nullptr, 10);
	if (contentLength > MaxBodyBytes) {
		status = 413;
		return false;
	}
	const auto length = static_cast<size_t>(contentLength);
	while (buffer.size() < length)
		if (!Receive(s, buffer))
			return false;
	request.Body = buffer.substr(0, length);
	buffer.erase(0, length);
	return true;
}

HttpResponse McpServer::Impl::Handle(const HttpRequest& request) {
	auto path = request.Path.substr(0, request.Path.find('?'));
	if (path != "/mcp" && path != "/mcp/")
		return { 404, R"({"error":"not found: the MCP endpoint is /mcp"})" };
	// (a web page in a browser must not get through: by DNS rebinding its Host is its own name, and its Origin is its site)
	if (!IsLoopbackHost(request.Header("host")))
		return { 403, R"({"error":"forbidden Host"})" };
	if (auto origin = LowerA(request.Header("origin")); !origin.empty()) {
		auto scheme = origin.find("://");
		if (scheme == std::string::npos || !IsLoopbackHost(origin.substr(scheme + 3)))
			return { 403, R"({"error":"forbidden Origin"})" };
	}
	auto authorization = request.Header("authorization");
	if (authorization.size() < 7 || LowerA(authorization.substr(0, 7)) != "bearer " || !ConstantTimeEquals(Trim(authorization.substr(7)), Token)) {
		HttpResponse response{ 401, R"({"error":"unauthorized: send Authorization: Bearer <the token DebugX shows in Options > MCP Server>"})" };
		response.Headers.push_back("WWW-Authenticate: Bearer realm=\"DebugX\"");
		return response;
	}
	if (request.Method != "POST") {
		HttpResponse response{ 405, R"JSON({"error":"POST JSON-RPC messages to /mcp (this server has no event stream)"})JSON" };
		response.Headers.push_back("Allow: POST");
		return response;
	}

	auto body = json::parse(request.Body, nullptr, false);
	if (body.is_discarded())
		return { 400, RpcError(nullptr, -32700, "Parse error").dump() };
	json answers = json::array();
	auto handle = [&](const json& message) {
		auto answer = HandleMessage(message);
		if (!answer.is_null())
			answers.push_back(std::move(answer));
	};
	if (body.is_array()) {
		for (auto& message : body)
			handle(message);
	}
	else
		handle(body);
	if (answers.empty())
		return { 202, {} };
	auto& result = body.is_array() ? answers : answers[0];
	return { 200, result.dump(-1, ' ', false, json::error_handler_t::replace) };
}

json McpServer::Impl::HandleMessage(const json& message) {
	if (!message.is_object())
		return RpcError(nullptr, -32600, "Invalid Request");
	const bool isRequest = message.contains("id") && !message["id"].is_null();
	if (!message.contains("method") || !message["method"].is_string())
		return isRequest ? RpcError(message["id"], -32600, "Invalid Request") : json();	// (a response of the client's: nothing asked)
	if (!isRequest)
		return {};		// notifications (initialized, cancelled): nothing to do
	const auto& id = message["id"];
	auto method = message["method"].get<std::string>();
	json params = message.contains("params") && message["params"].is_object() ? message["params"] : json::object();

	if (method == "initialize") {
		std::string version = ProtocolVersions[0];
		if (params.contains("protocolVersion") && params["protocolVersion"].is_string()) {
			auto asked = params["protocolVersion"].get<std::string>();
			if (std::find(std::begin(ProtocolVersions), std::end(ProtocolVersions), asked) != std::end(ProtocolVersions))
				version = asked;
		}
		return RpcResult(id, {
			{ "protocolVersion", version },
			{ "capabilities", { { "tools", { { "listChanged", false } } } } },
			{ "serverInfo", { { "name", "debugx" }, { "title", "DebugX" }, { "version", "1.0.0" } } },
			{ "instructions", ServerInstructions },
			});
	}
	if (method == "ping")
		return RpcResult(id, json::object());
	if (method == "tools/list") {
		json tools = json::array();
		for (auto& t : Tools()) {
			tools.push_back({
				{ "name", t.Name },
				{ "title", t.Title },
				{ "description", t.Description },
				{ "inputSchema", t.InputSchema },
				{ "annotations", { { "title", t.Title }, { "readOnlyHint", t.ReadOnly }, { "destructiveHint", t.Destructive },
					{ "idempotentHint", t.ReadOnly }, { "openWorldHint", false } } },
				});
		}
		return RpcResult(id, { { "tools", tools } });
	}
	if (method == "tools/call") {
		if (!params.contains("name") || !params["name"].is_string())
			return RpcError(id, -32602, "Invalid params: name");
		auto tool = FindTool(params["name"].get<std::string>());
		if (!tool)
			return RpcError(id, -32602, "Unknown tool: " + params["name"].get<std::string>());
		json args = params.contains("arguments") && params["arguments"].is_object() ? params["arguments"] : json::object();
		return RpcResult(id, CallTool(*tool, std::move(args)));
	}
	return RpcError(id, -32601, "Method not found: " + method);
}

std::optional<ToolResult> McpServer::Impl::Invoke(const std::string& name, ToolFunction function, json args, bool serialized, bool log,
	int timeoutSeconds, bool& stopping) {
	auto call = std::make_shared<Call>();
	auto shared = State;
	stopping = false;
	bool posted = Dispatcher->Post([ui = Ui, shared, call, name, function, args, serialized, log] {
		auto start = [=] {
			// (the Task is dropped: the coroutine finishes on its own)
			RunTool(ui, shared, call, name, function, args, serialized, log);
		};
		if (serialized)
			ui->Add(start);
		else
			start();
		});
	if (!posted) {
		stopping = true;
		return std::nullopt;
	}
	std::unique_lock lock(shared->Lock);
	shared->Changed.wait_for(lock, std::chrono::seconds(timeoutSeconds), [&] { return call->Done || shared->Stopping; });
	if (call->Done)
		return call->Result;
	stopping = shared->Stopping;
	return std::nullopt;
}

int McpServer::Impl::TargetState() {
	auto state = std::make_shared<int>(-2);
	auto shared = State;
	if (!Dispatcher->Post([ui = Ui, shared, state] {
		int value = !HasTarget(ui->Host) ? 2 : IsStopped(ui->Context()) ? 1 : 0;
		{
			std::lock_guard lock(shared->Lock);
			*state = value;
		}
		shared->Changed.notify_all();
		}))
		return -1;
	std::unique_lock lock(shared->Lock);
	shared->Changed.wait(lock, [&] { return *state != -2 || shared->Stopping; });
	return shared->Stopping ? -1 : *state;
}

bool McpServer::Impl::Pause(int ms) {
	std::unique_lock lock(State->Lock);
	return !State->Changed.wait_for(lock, std::chrono::milliseconds(ms), [&] { return State->Stopping; });
}

json McpServer::Impl::CallTool(const ToolDef& tool, json args) {
	auto text = [](std::string t, bool error) {
		return json{ { "content", json::array({ { { "type", "text" }, { "text", std::move(t) } } }) }, { "isError", error } };
	};
	const char* stoppingText = "DebugX's MCP server is stopping (DebugX is closing, or the server was turned off).";

	int timeout = tool.TimeoutSeconds;
	if (tool.TimeoutArgument && args.contains(tool.TimeoutArgument) && args[tool.TimeoutArgument].is_number())
		timeout = std::clamp(args[tool.TimeoutArgument].get<int>(), 0, tool.MaxTimeoutSeconds);

	bool stopping = false;
	if (tool.Kind != ToolKind::WaitForStop) {
		auto result = Invoke(tool.Name, tool.Run, args, tool.Kind == ToolKind::Serialized, true, timeout, stopping);
		if (result)
			return text(result->Text, result->IsError);
		if (stopping)
			return text(stoppingText, true);
		if (tool.TimeoutIsNormal)
			return text(std::format("The target is still running: it has not stopped within {} s. wait_for_stop waits for it to stop, "
				"break_into stops it.", timeout), false);
		return text(std::format("{} has not completed within {} s and goes on in DebugX (the debugger may be busy: a long command, symbols "
			"loading). The tools that wait their turn behind it cannot run until it has completed.", tool.Name, timeout), true);
	}

	// Break, then wait; or just wait: polling the target's status on the UI thread (the status comes from the debugger's
	// notifications there), then what StopSummary says
	auto first = Invoke(tool.Name, tool.Run, args, false, true, 30, stopping);
	if (!first)
		return text(stopping ? stoppingText : "The debugger did not answer.", true);
	if (first->IsError)
		return text(first->Text, true);
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout);
	while (true) {
		int state = TargetState();
		if (state < 0)
			return text(stoppingText, true);
		if (state > 0)
			break;
		if (std::chrono::steady_clock::now() >= deadline)
			return text(std::format("The target is still running: it has not stopped within {} s.", timeout), false);
		if (!Pause(100))
			return text(stoppingText, true);
	}
	auto summary = Invoke("stop summary", StopSummary, json::object(), true, false, 60, stopping);
	std::string out = first->Text.empty() ? std::string() : first->Text + "\n";
	out += summary ? summary->Text : std::string("The target has stopped (the debugger did not say where in time).");
	return text(out, false);
}

}
