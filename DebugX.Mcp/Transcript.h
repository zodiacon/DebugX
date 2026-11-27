#pragma once

//
// The Assistant's conversation as a Markdown document, for a Markdown view (the Assistant pane's CMarkdownViewCtrl): its
// events (AssistantEvent) in, the document out. Window-free, so the tests can check it.
//
//   - the user's messages after a rule, "**You:**" first;
//   - the model's text as it streams, with the symbols and addresses in it (module!function+0x10, 00007ff6`12345678,
//     0x7ff612345678) made links to dbgx:<expression> (LinkTarget), and '<' outside code escaped, so that a C++ type
//     (vector<int>) is not taken for HTML;
//   - a summary of the model's reasoning as a quote;
//   - a tool call as a line while it runs, then a <details> whose summary is the call and whose content is its result, in a
//     code block (the first lines of a long one);
//   - notes in italics, errors as a GitHub warning.
//

#include <string>
#include <string_view>
#include <vector>
#include "Assistant.h"

namespace DebugX::Ai {
	class Transcript {
	public:
		// one of the Assistant's events; true if the document changed
		bool Apply(const AssistantEvent& e);
		// a note of the pane's own (in italics)
		void AddNote(std::wstring_view text);
		void Clear();
		bool Empty() const {
			return m_Blocks.empty() && m_Current.empty();
		}
		// the document, as it is now (an answer that is still streaming included)
		std::wstring Markdown() const;

		// the model's Markdown with the symbols and addresses made links, and '<' outside code escaped (fenced code blocks
		// and code spans are left as they are, but a code span that is just a symbol or an address becomes a link)
		static std::wstring Linkify(std::wstring_view markdown);
		// the expression a link made by Linkify is for; empty if the URL is not one of those
		static std::wstring LinkTarget(std::wstring_view url);

		// most of a tool result that is shown (the model gets all of it)
		static constexpr size_t MaxResultLines = 300;
		static constexpr size_t MaxResultChars = 30000;

	private:
		enum class Kind { None, Text, Thinking };
		std::wstring Render(Kind kind, const std::wstring& text) const;
		void Finish();

		std::vector<std::wstring> m_Blocks;
		std::wstring m_Current;		// a block that is still streaming
		Kind m_Kind{ Kind::None };
		size_t m_ToolBlock{ std::wstring::npos };	// the block of the tool call that runs
		std::wstring m_ToolName, m_ToolArguments;
		bool m_HadUser{ false };
	};
}
