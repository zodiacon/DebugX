#pragma once

//
// The Claude Messages API (POST /v1/messages), as the Assistant uses it: one streamed request at a time, over WinHTTP --
// there is no Anthropic SDK for C++. The pieces are separate so that the tests can drive them: SseParser turns bytes into
// server-sent events, MessageBuilder turns the events into the message (as a non-streamed response would have it), and
// ITransport sends a request and hands over its events (CreateHttpTransport: the real one; the tests script their own).
//

#include <nlohmann/json.hpp>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace DebugX::Ai {
	using json = nlohmann::json;

	// the anthropic-version header
	inline constexpr const char* ApiVersion = "2026-10-21";

	// Server-sent events: bytes in (in any pieces), events out ("event:" and "data:" lines, ended by an empty line)
	class SseParser {
	public:
		using Handler = std::function<void(const std::string& event, const std::string& data)>;
		void Feed(std::string_view bytes, const Handler& handler);

	private:
		void Line(std::string_view line, const Handler& handler);

		std::string m_Partial;		// a line not ended yet
		std::string m_Event, m_Data;
		bool m_HasData{ false };
	};

	// The message a stream's events make: the content blocks in order (with the thinking blocks' signatures, to send back
	// as they are), the stop reason, the usage
	class MessageBuilder {
	public:
		// what an event changed, for whoever shows the answer as it comes
		struct Update {
			enum class Kind { None, BlockStart, Delta, BlockStop } Type{ Kind::None };
			size_t Index{ 0 };
			std::string BlockType;		// text, thinking, tool_use, fallback, ...
			std::string Text;			// Delta: the text added (text, thinking); BlockStart of a fallback: the model that went on
		};
		Update Apply(const std::string& event, const json& data);

		bool Complete() const {
			return m_Complete;		// message_stop has come
		}
		bool HasContent() const {
			return !m_Blocks.empty();
		}
		// an error event in the stream: its type (overloaded_error, ...) and message; empty: none
		const std::string& ErrorType() const {
			return m_ErrorType;
		}
		const std::string& ErrorMessage() const {
			return m_ErrorMessage;
		}
		// the tool calls whose input was not valid JSON (by their id): their input is {} in the message, this is what came
		const std::map<std::string, std::string>& InvalidInputs() const {
			return m_InvalidInputs;
		}
		json Message() const;

	private:
		json m_Message = json::object();
		std::vector<json> m_Blocks;
		std::vector<std::string> m_PartialJson;		// a tool_use block's input, as it streams
		std::map<std::string, std::string> m_InvalidInputs;
		std::string m_ErrorType, m_ErrorMessage;
		bool m_Complete{ false };
	};

	// What the conversation sends back of a response's content: all of it, as it came -- except that before the last
	// fallback block (a model declined, and another went on) the declined model's thinking and tool calls are left out, as
	// the API asks; the fallback block stays where it was.
	json EchoContent(const json& content);

	struct TransportResult {
		bool Ok{ false };			// the stream ran to its end (message_stop)
		bool Cancelled{ false };
		int HttpStatus{ 0 };
		std::string Error;			// why not, for the user
		bool Retryable{ false };	// overloaded, rate limited, a broken connection: worth another try
		int RetryAfterSeconds{ 0 };
	};

	class ITransport {
	public:
		virtual ~ITransport() = default;
		// (blocking: a worker thread) POSTs the request, streamed, with these anthropic-beta values; each event goes to
		// 'onEvent' as it comes (its data parsed)
		virtual TransportResult Send(const json& request, const std::vector<std::string>& betas,
			const std::function<void(const std::string& event, const json& data)>& onEvent) = 0;
		// (any thread) ends a Send that is under way, which then returns Cancelled -- or the next one, at once, if none is
		virtual void Cancel() = 0;
		// (before a Send is started) a Cancel that came before does not end it
		virtual void Rearm() = 0;
	};

	// https://api.anthropic.com, with this API key
	std::unique_ptr<ITransport> CreateHttpTransport(std::string apiKey);
}
