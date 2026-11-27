#include "pch.h"
#include "Transcript.h"
#include <regex>

namespace DebugX::Ai {

namespace {
	// (by number: the source stays ASCII -- MSVC reads it in the ANSI code page)
	constexpr wchar_t Triangle = 0x25B8;	// a tool call that ran
	constexpr wchar_t Cross = 0x2717;		// one that failed
	constexpr wchar_t Ellipsis = 0x2026;

	PCWSTR const Scheme = L"dbgx:";
	constexpr size_t MaxArgumentsShown = 200;

	// a symbol (module!function+0x10, with templates and C++ operators' characters), or an address
	const std::wregex& LinkPattern() {
		static const std::wregex pattern(
			LR"(([A-Za-z_][\w.\-]*![A-Za-z_?$@~][\w:<>~$@?]*(\+0x[0-9A-Fa-f]+)?)|\b[0-9A-Fa-f]{8}`[0-9A-Fa-f]{8}\b|\b0x[0-9A-Fa-f]{6,16}\b)");
		return pattern;
	}

	bool IsLink(std::wstring_view text) {
		return std::regex_match(text.begin(), text.end(), LinkPattern());
	}

	std::wstring Url(std::wstring_view expression) {
		std::wstring url(Scheme);
		for (wchar_t c : expression) {
			if (iswalnum(c) && c < 128 || wcschr(L"!._~-+:", c))
				url += c;
			else
				url += std::format(L"%{:02X}", static_cast<unsigned>(c) & 0xFF);
		}
		return url;
	}

	// a code span for any text (backticks in it: a longer fence, and spaces at its ends)
	std::wstring CodeSpan(std::wstring_view text) {
		size_t longest = 0, run = 0;
		for (wchar_t c : text) {
			run = c == L'`' ? run + 1 : 0;
			longest = std::max(longest, run);
		}
		std::wstring fence(longest + 1, L'`');
		const bool pad = !text.empty() && (text.front() == L'`' || text.back() == L'`');
		return fence + (pad ? L" " : L"") + std::wstring(text) + (pad ? L" " : L"") + fence;
	}

	std::wstring Link(std::wstring_view expression) {
		return L"[" + CodeSpan(expression) + L"](" + Url(expression) + L")";
	}

	// Markdown's punctuation escaped (text of DebugX's own: notes, errors)
	std::wstring EscapeMarkdown(std::wstring_view text) {
		std::wstring out;
		for (wchar_t c : text) {
			if (wcschr(L"\\`*_[]<>#|~", c) && c)
				out += L'\\';
			out += c;
		}
		return out;
	}

	std::wstring EscapeHtml(std::wstring_view text) {
		std::wstring out;
		for (wchar_t c : text) {
			switch (c) {
				case L'<': out += L"&lt;"; break;
				case L'>': out += L"&gt;"; break;
				case L'&': out += L"&amp;"; break;
				case L'"': out += L"&quot;"; break;
				case L'\r': break;
				case L'\n': out += L' '; break;
				default: out += c;
			}
		}
		return out;
	}

	// every line after the prefix
	std::wstring Prefixed(std::wstring_view text, std::wstring_view prefix) {
		std::wstring out(prefix);
		for (wchar_t c : text) {
			if (c == L'\r')
				continue;
			out += c;
			if (c == L'\n')
				out += prefix;
		}
		return out;
	}

	// a run of prose (not code): its links, and '<' as text
	std::wstring LinkifyProse(std::wstring_view text) {
		// (a Markdown link of the model's own is left as it is)
		if (text.find(L"](") != std::wstring_view::npos)
			return std::wstring(text);
		std::wstring out;
		auto escape = [&](std::wstring_view part) {
			for (wchar_t c : part)
				out += c == L'<' ? std::wstring(L"&lt;") : std::wstring(1, c);
		};
		size_t last = 0;
		for (auto it = std::wcregex_iterator(text.data(), text.data() + text.size(), LinkPattern()); it != std::wcregex_iterator(); ++it) {
			escape(text.substr(last, it->position() - last));
			out += Link(text.substr(it->position(), it->length()));
			last = it->position() + it->length();
		}
		escape(text.substr(last));
		return out;
	}

	std::wstring LinkifyLine(std::wstring_view line) {
		std::wstring out;
		size_t i = 0;
		while (i < line.size()) {
			if (line[i] == L'`') {
				size_t run = 0;
				while (i + run < line.size() && line[i + run] == L'`')
					run++;
				// the code span's end: a run of as many backticks
				size_t close = i + run;
				bool found = false;
				while ((close = line.find(L'`', close)) != std::wstring_view::npos) {
					size_t n = 0;
					while (close + n < line.size() && line[close + n] == L'`')
						n++;
					if (n == run) {
						found = true;
						break;
					}
					close += n;
				}
				if (!found) {
					out.append(run, L'`');
					i += run;
					continue;
				}
				auto content = line.substr(i + run, close - i - run);
				auto trimmed = content;
				while (!trimmed.empty() && trimmed.front() == L' ')
					trimmed.remove_prefix(1);
				while (!trimmed.empty() && trimmed.back() == L' ')
					trimmed.remove_suffix(1);
				if (IsLink(trimmed))
					out += Link(trimmed);
				else
					out += line.substr(i, close + run - i);
				i = close + run;
				continue;
			}
			auto next = line.find(L'`', i);
			if (next == std::wstring_view::npos)
				next = line.size();
			out += LinkifyProse(line.substr(i, next - i));
			i = next;
		}
		return out;
	}
}

std::wstring Transcript::Linkify(std::wstring_view markdown) {
	std::wstring out;
	bool inFence = false;
	wchar_t fenceChar = 0;
	size_t fenceLength = 0;
	size_t pos = 0;
	while (pos < markdown.size()) {
		auto end = markdown.find(L'\n', pos);
		const bool last = end == std::wstring_view::npos;
		auto line = markdown.substr(pos, (last ? markdown.size() : end) - pos);
		pos = last ? markdown.size() : end + 1;

		auto trimmed = line;
		for (int n = 0; n < 3 && !trimmed.empty() && trimmed.front() == L' '; n++)
			trimmed.remove_prefix(1);
		size_t run = 0;
		if (!trimmed.empty() && (trimmed.front() == L'`' || trimmed.front() == L'~'))
			while (run < trimmed.size() && trimmed[run] == trimmed.front())
				run++;
		if (!inFence && run >= 3) {
			inFence = true;
			fenceChar = trimmed.front();
			fenceLength = run;
			out += line;
		}
		else if (inFence) {
			if (run >= fenceLength && trimmed.front() == fenceChar && trimmed.find_first_not_of(L" \t\r", run) == std::wstring_view::npos)
				inFence = false;
			out += line;
		}
		else
			out += LinkifyLine(line);
		if (!last)
			out += L'\n';
	}
	return out;
}

std::wstring Transcript::LinkTarget(std::wstring_view url) {
	if (!url.starts_with(Scheme))
		return {};
	url.remove_prefix(wcslen(Scheme));
	std::wstring expression;
	for (size_t i = 0; i < url.size(); i++) {
		if (url[i] == L'%' && i + 2 < url.size() + 0 && iswxdigit(url[i + 1]) && iswxdigit(url[i + 2])) {
			expression += static_cast<wchar_t>(std::stoi(std::wstring(url.substr(i + 1, 2)), nullptr, 16));
			i += 2;
		}
		else
			expression += url[i];
	}
	return expression;
}

std::wstring Transcript::Render(Kind kind, const std::wstring& text) const {
	if (kind == Kind::Thinking) {
		auto body = Linkify(text);
		while (!body.empty() && (body.back() == L'\n' || body.back() == L'\r'))
			body.pop_back();
		return Prefixed(L"*Thinking:* " + body, L"> ");
	}
	return Linkify(text);
}

void Transcript::Finish() {
	if (m_Kind != Kind::None && m_Current.find_first_not_of(L" \t\r\n") != std::wstring::npos)
		m_Blocks.push_back(Render(m_Kind, m_Current));
	m_Current.clear();
	m_Kind = Kind::None;
}

void Transcript::AddNote(std::wstring_view text) {
	Finish();
	m_Blocks.push_back(L"*" + EscapeMarkdown(text) + L"*");
}

void Transcript::Clear() {
	m_Blocks.clear();
	m_Current.clear();
	m_Kind = Kind::None;
	m_ToolBlock = std::wstring::npos;
	m_HadUser = false;
}

bool Transcript::Apply(const AssistantEvent& e) {
	using EventKind = AssistantEvent::Kind;
	switch (e.Type) {
		case EventKind::User:
			Finish();
			m_Blocks.push_back((m_HadUser ? L"---\n\n" : L"") + std::wstring(L"**You:** ") + Linkify(e.Text));
			m_HadUser = true;
			return true;

		case EventKind::TextStart:
		case EventKind::ThinkingStart:
			Finish();
			m_Kind = e.Type == EventKind::TextStart ? Kind::Text : Kind::Thinking;
			return false;
		case EventKind::Text:
		case EventKind::Thinking:
			if (m_Kind == Kind::None)
				m_Kind = e.Type == EventKind::Text ? Kind::Text : Kind::Thinking;
			m_Current += e.Text;
			return true;
		case EventKind::BlockEnd:
			Finish();
			return true;

		case EventKind::ToolCall: {
			Finish();
			m_ToolName = e.Name;
			m_ToolArguments = e.Text == L"{}" ? std::wstring() : e.Text;
			if (m_ToolArguments.size() > MaxArgumentsShown)
				m_ToolArguments = m_ToolArguments.substr(0, MaxArgumentsShown) + Ellipsis;
			m_Blocks.push_back(std::wstring(1, Triangle) + L" " + CodeSpan(e.Name)
				+ (m_ToolArguments.empty() ? L"" : L" " + CodeSpan(m_ToolArguments)) + L" *" + Ellipsis + L"*");
			m_ToolBlock = m_Blocks.size() - 1;
			return true;
		}
		case EventKind::ToolResult: {
			Finish();
			// the result, its first lines (a long one is cut), in a code block that its text cannot end
			std::wstring result = e.Text;
			size_t lines = 0, cut = std::wstring::npos;
			for (size_t i = 0; i < result.size(); i++) {
				if (result[i] == L'\n' && ++lines >= MaxResultLines) {
					cut = i;
					break;
				}
				if (i >= MaxResultChars) {
					cut = i;
					break;
				}
			}
			if (cut != std::wstring::npos) {
				const auto more = std::count(result.begin() + cut, result.end(), L'\n');
				result = result.substr(0, cut) + std::format(L"\n{} ({} more lines)", Ellipsis, more);
			}
			size_t longest = 0, run = 0;
			for (wchar_t c : result) {
				run = c == L'`' ? run + 1 : 0;
				longest = std::max(longest, run);
			}
			std::wstring fence(std::max<size_t>(3, longest + 1), L'`');
			auto summary = std::wstring(1, e.Failed ? Cross : Triangle) + L" " + EscapeHtml(e.Name)
				+ (m_ToolArguments.empty() || e.Name != m_ToolName ? L"" : L" " + EscapeHtml(m_ToolArguments));
			std::wstring block = L"<details><summary>" + summary + L"</summary>\n\n" + fence + L"text\n"
				+ (result.empty() ? std::wstring(L"(nothing)") : result) + L"\n" + fence + L"\n\n</details>";
			if (e.Failed) {
				// (why it failed, without opening it)
				auto first = e.Text.substr(0, e.Text.find(L'\n'));
				if (first.size() > 300)
					first = first.substr(0, 300) + Ellipsis;
				block += L"\n\n*" + EscapeMarkdown(first) + L"*";
			}
			if (m_ToolBlock < m_Blocks.size())
				m_Blocks[m_ToolBlock] = std::move(block);
			else
				m_Blocks.push_back(std::move(block));
			m_ToolBlock = std::wstring::npos;
			return true;
		}

		case EventKind::Info:
			AddNote(e.Text);
			return true;
		case EventKind::Error:
			Finish();
			m_Blocks.push_back(Prefixed(L"[!WARNING]\n" + EscapeMarkdown(e.Text), L"> "));
			return true;

		default:
			return false;
	}
}

std::wstring Transcript::Markdown() const {
	std::wstring doc;
	for (auto& block : m_Blocks) {
		if (!doc.empty())
			doc += L"\n\n";
		doc += block;
	}
	if (m_Kind != Kind::None && !m_Current.empty()) {
		if (!doc.empty())
			doc += L"\n\n";
		doc += Render(m_Kind, m_Current);
	}
	return doc;
}

}
