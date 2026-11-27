#include "MiParser.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>

namespace GdbEngine {
	namespace {
		class Parser {
		public:
			explicit Parser(std::string_view text) : m_Text(text) {}

			bool AtEnd() const {
				return m_Pos >= m_Text.size();
			}
			char Peek() const {
				return AtEnd() ? '\0' : m_Text[m_Pos];
			}
			bool Accept(char c) {
				if (Peek() == c) {
					m_Pos++;
					return true;
				}
				return false;
			}
			std::string_view Rest() const {
				return m_Text.substr(std::min(m_Pos, m_Text.size()));
			}

			// a c-string (the opening quote is next)
			std::string CString() {
				std::string result;
				if (!Accept('"'))
					return result;
				while (!AtEnd()) {
					char c = m_Text[m_Pos++];
					if (c == '"')
						break;
					if (c != '\\') {
						result += c;
						continue;
					}
					if (AtEnd())
						break;
					c = m_Text[m_Pos++];
					switch (c) {
						case 'n': result += '\n'; break;
						case 't': result += '\t'; break;
						case 'r': result += '\r'; break;
						case 'a': result += '\a'; break;
						case 'b': result += '\b'; break;
						case 'f': result += '\f'; break;
						case 'v': result += '\v'; break;
						case 'e': result += '\x1b'; break;
						case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
							// up to three octal digits: GDB writes bytes above 0x7f so (UTF-8 text comes out as its bytes)
							int value = c - '0';
							for (int i = 0; i < 2 && Peek() >= '0' && Peek() <= '7'; i++)
								value = value * 8 + (m_Text[m_Pos++] - '0');
							result += static_cast<char>(value);
							break;
						}
						default: result += c; break;		// \" \\ and the rest
					}
				}
				return result;
			}

			std::string Name() {
				auto start = m_Pos;
				while (!AtEnd() && Peek() != '=' && Peek() != ',' && Peek() != '}' && Peek() != ']')
					m_Pos++;
				return std::string(m_Text.substr(start, m_Pos - start));
			}

			MiValue Value() {
				MiValue value;
				if (Peek() == '"') {
					value.Text = CString();
				}
				else if (Accept('{')) {
					value.Type = MiValue::Kind::Tuple;
					if (!Accept('}')) {
						do {
							auto name = Name();
							Accept('=');
							value.Items.emplace_back(std::move(name), Value());
						} while (Accept(','));
						Accept('}');
					}
				}
				else if (Accept('[')) {
					value.Type = MiValue::Kind::List;
					if (!Accept(']')) {
						do {
							if (Peek() == '"' || Peek() == '{' || Peek() == '[')
								value.Items.emplace_back(std::string(), Value());
							else {
								auto name = Name();
								Accept('=');
								value.Items.emplace_back(std::move(name), Value());
							}
						} while (Accept(','));
						Accept(']');
					}
				}
				return value;
			}

			// ,name=value,name=value... to the end
			MiValue Results() {
				MiValue results;
				results.Type = MiValue::Kind::Tuple;
				while (Accept(',')) {
					auto name = Name();
					Accept('=');
					results.Items.emplace_back(std::move(name), Value());
				}
				return results;
			}

			std::string Class() {
				auto start = m_Pos;
				while (!AtEnd() && Peek() != ',')
					m_Pos++;
				return std::string(m_Text.substr(start, m_Pos - start));
			}

			size_t m_Pos{ 0 };

		private:
			std::string_view m_Text;
		};
	}

	const MiValue& MiValue::Empty() {
		static const MiValue empty;
		return empty;
	}

	const MiValue* MiValue::Find(std::string_view name) const {
		for (auto& [n, v] : Items)
			if (n == name)
				return &v;
		return nullptr;
	}

	const MiValue& MiValue::operator[](std::string_view name) const {
		auto v = Find(name);
		return v ? *v : Empty();
	}

	std::string MiValue::Get(std::string_view name) const {
		auto v = Find(name);
		return v && v->IsString() ? v->Text : std::string();
	}

	std::uint64_t MiValue::GetNumber(std::string_view name, std::uint64_t fallback, int base) const {
		auto v = Find(name);
		return v && v->IsString() ? ParseNumber(v->Text, fallback, base) : fallback;
	}

	std::uint64_t ParseNumber(std::string_view text, std::uint64_t fallback, int base) {
		while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
			text.remove_prefix(1);
		if (text.empty())
			return fallback;
		std::string s(text);
		char* end = nullptr;
		errno = 0;
		std::uint64_t value;
		if (s[0] == '-')
			value = static_cast<std::uint64_t>(std::strtoll(s.c_str(), &end, base));
		else
			value = std::strtoull(s.c_str(), &end, base);
		if (end == s.c_str() || errno == ERANGE)
			return fallback;
		return value;
	}

	MiRecord ParseMiLine(std::string_view line) {
		MiRecord record;
		if (!line.empty() && line.back() == '\r')
			line.remove_suffix(1);

		auto trimmed = line;
		while (!trimmed.empty() && trimmed.back() == ' ')
			trimmed.remove_suffix(1);
		if (trimmed == "(gdb)") {
			record.Type = MiRecord::Kind::Prompt;
			return record;
		}

		Parser p(line);
		size_t digits = 0;
		while (digits < line.size() && std::isdigit(static_cast<unsigned char>(line[digits])))
			digits++;
		if (digits > 0 && digits < line.size()) {
			record.Token = std::strtoull(std::string(line.substr(0, digits)).c_str(), nullptr, 10);
			p.m_Pos = digits;
		}

		switch (p.Peek()) {
			case '^': record.Type = MiRecord::Kind::Result; break;
			case '*': record.Type = MiRecord::Kind::Exec; break;
			case '+': record.Type = MiRecord::Kind::Status; break;
			case '=': record.Type = MiRecord::Kind::Notify; break;
			case '~': record.Type = MiRecord::Kind::Console; break;
			case '@': record.Type = MiRecord::Kind::Target; break;
			case '&': record.Type = MiRecord::Kind::Log; break;
			default:
				// not MI (GDB writes some warnings as they are, and the inferior may write here if it shares GDB's terminal)
				record.Type = MiRecord::Kind::Other;
				record.Token.reset();
				record.Text = std::string(line);
				return record;
		}
		p.m_Pos++;

		if (record.Type == MiRecord::Kind::Console || record.Type == MiRecord::Kind::Target || record.Type == MiRecord::Kind::Log) {
			record.Token.reset();
			record.Text = p.CString();
			return record;
		}

		record.Class = p.Class();
		record.Results = p.Results();
		return record;
	}

	std::string MiQuote(std::string_view text) {
		std::string result;
		result.reserve(text.size() + 2);
		result += '"';
		for (char c : text) {
			switch (c) {
				case '"': result += "\\\""; break;
				case '\\': result += "\\\\"; break;
				case '\n': result += "\\n"; break;
				case '\r': result += "\\r"; break;
				case '\t': result += "\\t"; break;
				default: result += c; break;
			}
		}
		result += '"';
		return result;
	}
}
