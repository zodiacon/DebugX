#include "pch.h"
#include "ClaudeApi.h"
#include <winhttp.h>
#include <atomic>

#pragma comment(lib, "winhttp.lib")

namespace DebugX::Ai {

// ---- SseParser

void SseParser::Feed(std::string_view bytes, const Handler& handler) {
	while (!bytes.empty()) {
		auto end = bytes.find('\n');
		if (end == std::string_view::npos) {
			m_Partial.append(bytes);
			return;
		}
		m_Partial.append(bytes.substr(0, end));
		bytes.remove_prefix(end + 1);
		std::string line;
		line.swap(m_Partial);
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		Line(line, handler);
	}
}

void SseParser::Line(std::string_view line, const Handler& handler) {
	if (line.empty()) {
		// (the end of an event)
		if (m_HasData || !m_Event.empty())
			handler(m_Event.empty() ? std::string("message") : m_Event, m_Data);
		m_Event.clear();
		m_Data.clear();
		m_HasData = false;
		return;
	}
	if (line[0] == ':')
		return;		// a comment
	auto colon = line.find(':');
	auto field = line.substr(0, colon);
	std::string_view value;
	if (colon != std::string_view::npos) {
		value = line.substr(colon + 1);
		if (!value.empty() && value[0] == ' ')
			value.remove_prefix(1);
	}
	if (field == "event")
		m_Event = value;
	else if (field == "data") {
		if (m_HasData)
			m_Data += '\n';
		m_Data += value;
		m_HasData = true;
	}
}

// ---- MessageBuilder

MessageBuilder::Update MessageBuilder::Apply(const std::string& event, const json& data) {
	Update update;
	if (event == "message_start" && data.contains("message")) {
		m_Message = data["message"];
		m_Message.erase("content");
	}
	else if (event == "content_block_start") {
		auto index = data.value("index", size_t(0));
		if (m_Blocks.size() <= index) {
			m_Blocks.resize(index + 1);
			m_PartialJson.resize(index + 1);
		}
		m_Blocks[index] = data.value("content_block", json::object());
		update.Type = Update::Kind::BlockStart;
		update.Index = index;
		update.BlockType = m_Blocks[index].value("type", "");
		if (update.BlockType == "fallback" && m_Blocks[index].contains("to") && m_Blocks[index]["to"].is_object())
			update.Text = m_Blocks[index]["to"].value("model", "");
	}
	else if (event == "content_block_delta") {
		auto index = data.value("index", size_t(0));
		if (index >= m_Blocks.size() || !data.contains("delta"))
			return update;
		auto& block = m_Blocks[index];
		const auto& delta = data["delta"];
		auto type = delta.value("type", "");
		update.Index = index;
		update.BlockType = block.value("type", "");
		if (type == "text_delta") {
			auto text = delta.value("text", "");
			block["text"] = block.value("text", "") + text;
			update.Type = Update::Kind::Delta;
			update.Text = std::move(text);
		}
		else if (type == "thinking_delta") {
			auto text = delta.value("thinking", "");
			block["thinking"] = block.value("thinking", "") + text;
			update.Type = Update::Kind::Delta;
			update.Text = std::move(text);
		}
		else if (type == "signature_delta")
			block["signature"] = block.value("signature", "") + delta.value("signature", "");
		else if (type == "input_json_delta")
			m_PartialJson[index] += delta.value("partial_json", "");
		else if (type == "compaction_delta") {
			// (a compaction block's summary, whole -- or the block may come complete in its content_block_start)
			if (delta.contains("content") && !delta["content"].is_null())
				block["content"] = delta["content"];
		}
		else if (type == "citations_delta" && delta.contains("citation")) {
			if (!block.contains("citations") || !block["citations"].is_array())
				block["citations"] = json::array();
			block["citations"].push_back(delta["citation"]);
		}
	}
	else if (event == "content_block_stop") {
		auto index = data.value("index", size_t(0));
		if (index >= m_Blocks.size())
			return update;
		auto& block = m_Blocks[index];
		if (block.value("type", "") == "tool_use" && !m_PartialJson[index].empty()) {
			// (strictly: an input cut short, or not valid, is not run -- the model hears why)
			auto input = json::parse(m_PartialJson[index], nullptr, false);
			if (input.is_discarded() || !input.is_object()) {
				m_InvalidInputs[block.value("id", "")] = m_PartialJson[index];
				block["input"] = json::object();
			}
			else
				block["input"] = std::move(input);
		}
		update.Type = Update::Kind::BlockStop;
		update.Index = index;
		update.BlockType = block.value("type", "");
	}
	else if (event == "message_delta") {
		if (data.contains("delta") && data["delta"].is_object())
			for (auto& [key, value] : data["delta"].items())
				m_Message[key] = value;
		if (data.contains("usage") && data["usage"].is_object()) {
			if (!m_Message.contains("usage") || !m_Message["usage"].is_object())
				m_Message["usage"] = json::object();
			for (auto& [key, value] : data["usage"].items())
				if (!value.is_null())
					m_Message["usage"][key] = value;
		}
	}
	else if (event == "message_stop")
		m_Complete = true;
	else if (event == "error" && data.contains("error")) {
		m_ErrorType = data["error"].value("type", "error");
		m_ErrorMessage = data["error"].value("message", "");
	}
	return update;
}

json MessageBuilder::Message() const {
	json message = m_Message;
	json content = json::array();
	for (auto& block : m_Blocks)
		if (!block.is_null())
			content.push_back(block);
	message["content"] = std::move(content);
	return message;
}

// ---- EchoContent

json EchoContent(const json& content) {
	if (!content.is_array())
		return json::array();
	size_t last = std::string::npos;
	for (size_t i = 0; i < content.size(); i++)
		if (content[i].value("type", "") == "fallback")
			last = i;
	if (last == std::string::npos)
		return content;
	json echo = json::array();
	for (size_t i = 0; i < content.size(); i++) {
		auto type = content[i].value("type", "");
		// (before the boundary: the text, and the boundary itself; the declined model's thinking and calls are not sent)
		if (i < last && type != "text" && type != "fallback")
			continue;
		echo.push_back(content[i]);
	}
	return echo;
}

// ---- the WinHTTP transport

namespace {
	std::wstring Widen(std::string_view text) {
		if (text.empty())
			return {};
		int n = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
		std::wstring result(n, L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), n);
		return result;
	}

	std::string WinHttpErrorText(DWORD error) {
		switch (error) {
			case ERROR_WINHTTP_TIMEOUT: return "the connection timed out";
			case ERROR_WINHTTP_NAME_NOT_RESOLVED: return "api.anthropic.com could not be found (no network?)";
			case ERROR_WINHTTP_CANNOT_CONNECT: return "could not connect to api.anthropic.com";
			case ERROR_WINHTTP_CONNECTION_ERROR: return "the connection was broken";
			case ERROR_WINHTTP_SECURE_FAILURE: return "the secure connection failed (a proxy or a certificate problem?)";
			default: return std::format("network error {}", error);
		}
	}

	class HttpTransport final : public ITransport {
	public:
		explicit HttpTransport(std::string apiKey) : m_Key(std::move(apiKey)) {
			m_Session = ::WinHttpOpen(L"DebugX", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
			if (!m_Session)
				m_Session = ::WinHttpOpen(L"DebugX", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		}
		~HttpTransport() override {
			Cancel();
			if (m_Session)
				::WinHttpCloseHandle(m_Session);
		}

		TransportResult Send(const json& request, const std::vector<std::string>& betas,
			const std::function<void(const std::string& event, const json& data)>& onEvent) override;

		void Cancel() override {
			std::lock_guard lock(m_Lock);
			m_Cancelled = true;
			// (closing the request ends a WinHTTP call that waits on it, on the other thread)
			if (m_Request)
				::WinHttpCloseHandle(std::exchange(m_Request, nullptr));
		}
		void Rearm() override {
			std::lock_guard lock(m_Lock);
			m_Cancelled = false;
		}

	private:
		// the request handle, unless Cancel has closed it already
		void CloseRequest(HINTERNET request) {
			std::lock_guard lock(m_Lock);
			if (m_Request == request) {
				::WinHttpCloseHandle(request);
				m_Request = nullptr;
			}
		}
		bool IsCancelled() {
			std::lock_guard lock(m_Lock);
			return m_Cancelled;
		}

		std::string m_Key;
		HINTERNET m_Session{ nullptr };
		std::mutex m_Lock;
		HINTERNET m_Request{ nullptr };
		bool m_Cancelled{ false };
	};

	TransportResult HttpTransport::Send(const json& request, const std::vector<std::string>& betas,
		const std::function<void(const std::string& event, const json& data)>& onEvent) {
		TransportResult result;
		if (!m_Session) {
			result.Error = WinHttpErrorText(::GetLastError());
			return result;
		}
		auto connect = ::WinHttpConnect(m_Session, L"api.anthropic.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
		if (!connect) {
			result.Error = WinHttpErrorText(::GetLastError());
			result.Retryable = true;
			return result;
		}
		auto handle = ::WinHttpOpenRequest(connect, L"POST", L"/v1/messages", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
		if (!handle) {
			result.Error = WinHttpErrorText(::GetLastError());
			::WinHttpCloseHandle(connect);
			return result;
		}
		{
			std::lock_guard lock(m_Lock);
			if (m_Cancelled) {
				::WinHttpCloseHandle(handle);
				::WinHttpCloseHandle(connect);
				result.Cancelled = true;
				return result;
			}
			m_Request = handle;
		}
		// (a long answer streams for minutes; pings keep the stream from being idle that long)
		::WinHttpSetTimeouts(handle, 30000, 30000, 60000, 300000);

		std::string headers = "content-type: application/json\r\naccept: text/event-stream\r\nanthropic-version: ";
		headers += ApiVersion;
		headers += "\r\nx-api-key: " + m_Key + "\r\n";
		if (!betas.empty()) {
			headers += "anthropic-beta: ";
			for (size_t i = 0; i < betas.size(); i++)
				headers += (i ? "," : "") + betas[i];
			headers += "\r\n";
		}
		auto wideHeaders = Widen(headers);
		auto body = request.dump(-1, ' ', false, json::error_handler_t::replace);

		auto fail = [&](DWORD error) {
			if (IsCancelled())
				result.Cancelled = true;
			else {
				result.Error = WinHttpErrorText(error);
				result.Retryable = true;
			}
		};
		if (!::WinHttpSendRequest(handle, wideHeaders.c_str(), static_cast<DWORD>(-1), body.data(), static_cast<DWORD>(body.size()),
			static_cast<DWORD>(body.size()), 0) || !::WinHttpReceiveResponse(handle, nullptr)) {
			fail(::GetLastError());
			CloseRequest(handle);
			::WinHttpCloseHandle(connect);
			return result;
		}

		DWORD status = 0, size = sizeof(status);
		::WinHttpQueryHeaders(handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
		result.HttpStatus = static_cast<int>(status);
		wchar_t retryAfter[32]{};
		size = sizeof(retryAfter);
		if (::WinHttpQueryHeaders(handle, WINHTTP_QUERY_CUSTOM, L"retry-after", retryAfter, &size, WINHTTP_NO_HEADER_INDEX))
			result.RetryAfterSeconds = _wtoi(retryAfter);

		SseParser parser;
		std::string errorBody;
		bool stopped = false;
		std::vector<char> buffer(16384);
		while (true) {
			DWORD read = 0;
			if (!::WinHttpReadData(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
				fail(::GetLastError());
				break;
			}
			if (read == 0)
				break;
			if (status != 200) {
				errorBody.append(buffer.data(), read);
				continue;
			}
			parser.Feed(std::string_view(buffer.data(), read), [&](const std::string& event, const std::string& data) {
				auto parsed = json::parse(data, nullptr, false);
				if (parsed.is_discarded())
					return;
				if (event == "message_stop")
					stopped = true;
				if (event == "error" && parsed.contains("error")) {
					auto type = parsed["error"].value("type", "");
					result.Error = parsed["error"].value("message", type);
					result.Retryable = type == "overloaded_error" || type == "api_error" || type == "rate_limit_error";
				}
				onEvent(event, parsed);
			});
		}
		CloseRequest(handle);
		::WinHttpCloseHandle(connect);

		if (result.Cancelled)
			return result;
		if (status != 200) {
			auto error = json::parse(errorBody, nullptr, false);
			std::string message;
			if (!error.is_discarded() && error.contains("error") && error["error"].is_object())
				message = error["error"].value("message", "");
			if (message.empty())
				message = errorBody.substr(0, 300);
			result.Error = std::format("HTTP {}: {}", status, message);
			if (status == 401)
				result.Error += " -- check the API key (the Assistant's Settings)";
			result.Retryable = status == 408 || status == 409 || status == 429 || status >= 500;
			return result;
		}
		if (stopped && result.Error.empty())
			result.Ok = true;
		else if (result.Error.empty()) {
			result.Error = "the connection ended before the answer did";
			result.Retryable = true;
		}
		return result;
	}
}

std::unique_ptr<ITransport> CreateHttpTransport(std::string apiKey) {
	return std::make_unique<HttpTransport>(std::move(apiKey));
}

}
