#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

//
// GDB/MI output (mi3), as the GDB manual's "GDB/MI Output Syntax" has it:
//
//   [token] ^done|running|connected|error|exit [, results]     a command's result
//   [token] *running|stopped [, results]                       exec-async: the target ran or stopped
//   [token] +... / =... [, results]                            status-async / notify-async
//   ~"..."  @"..."  &"..."                                     console / target / log streams
//   (gdb)                                                      the prompt
//
namespace GdbEngine {
	struct MiItem;

	// a value: a c-string, a tuple {name=value,...} or a list [value,...] / [name=value,...]
	struct MiValue {
		enum class Kind { String, Tuple, List };
		Kind Type{ Kind::String };
		std::string Text;
		// tuple and list items; a list of plain values has empty names
		// (not std::pair: a pair of MiValue cannot be named inside MiValue, which is incomplete there)
		std::vector<MiItem> Items;

		bool IsString() const { return Type == Kind::String; }
		bool IsTuple() const { return Type == Kind::Tuple; }
		bool IsList() const { return Type == Kind::List; }

		// the first item with the name (tuple, or a list of results); null if none
		const MiValue* Find(std::string_view name) const;
		// that item, or an empty value
		const MiValue& operator[](std::string_view name) const;
		// that item's text ("" if none, or it is not a string)
		std::string Get(std::string_view name) const;
		bool Has(std::string_view name) const { return Find(name) != nullptr; }
		// a number in the item (decimal, 0x hex, or 0 octal as MI writes exit codes); 'fallback' if none
		std::uint64_t GetNumber(std::string_view name, std::uint64_t fallback = 0, int base = 0) const;

		static const MiValue& Empty();
	};

	// a named item of a tuple or list (named as std::pair's members, which is what it stands for)
	struct MiItem {
		std::string first;
		MiValue second;
	};

	struct MiRecord {
		enum class Kind { Result, Exec, Status, Notify, Console, Target, Log, Prompt, Other };
		Kind Type{ Kind::Other };
		std::optional<std::uint64_t> Token;
		std::string Class;		// done, running, stopped, breakpoint-created, ...
		MiValue Results;		// a tuple of the results
		std::string Text;		// a stream's text (unescaped), or the line itself for Other
	};

	// one line of GDB's output, without its line end
	MiRecord ParseMiLine(std::string_view line);

	// a c-string as MI takes it in a command's parameters: "...", with \ " and control characters escaped
	std::string MiQuote(std::string_view text);

	// parses a number as GDB writes it ("0x7ffff7dd", "42", "-1"); 'fallback' if it is not one
	std::uint64_t ParseNumber(std::string_view text, std::uint64_t fallback = 0, int base = 0);
}
