#include "pch.h"
#include "McpTools.h"
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>

using namespace DebugEngine;

namespace DebugX::Mcp {

const char* const ServerInstructions =
R"MCP(DebugX is a Windows debugger (on the Windows debug engine, DbgEng) with a target the user has started: a live process,
a dump file or a kernel. These tools look into that target.

- Call get_session first: it says what is being debugged and whether it is stopped. Most tools need the target stopped;
  if it runs, break_into stops it (only when the user wants that: the program is paused for everyone).
- Thread ids are the debugger's (the [N] of list_threads, as in WinDbg's ~N), not the system's thread ids (tid).
- For an overview of what every thread is doing, or to find threads in some module or function, use
  get_all_call_stacks (with filter, e.g. "mymodule!*" or "*Lock*"); it groups threads that have the same stack.
- get_locals and evaluate show values as the debugger's data model does (dx): C++ expressions, with NatVis
  visualizers for the standard library where the engine has them; depth expands members.
- Addresses can be given as numbers or as debugger expressions: "0x7ff6`12340000", "kernel32!CreateFileW", "@rsp+0x20",
  "poi(@rcx)", "@$csp".
- run_command runs any WinDbg command and returns its output: !analyze -v, !heap -s, !locks, dt module!Type addr,
  dps @rsp, ub @rip, !peb, lm, .lastevent, !process 0 0 (kernel), !thread, !irp... The !-commands need the Windows Kits
  debugger's engine (get_session says which kind of target it is; a failing !-command says so). Do not resume the target
  with run_command (g, p, t): use go, step and run_to, which wait for it to stop and say where it is.
- go, step, run_to, set_breakpoint and remove_breakpoint change the debugging session the user is looking at: use them
  when asked to, or after saying why.
- Results are text; long ones are cut short (narrow the request: a filter, fewer frames, a smaller size).)MCP";

namespace {
	constexpr size_t MaxResultChars = 120000;

	// ---- text

	std::string Hex(ULONG64 value) {
		return std::format("0x{:X}", value);
	}

	std::string Address(ULONG64 value) {
		return value > 0xFFFFFFFFull ? std::format("{:08X}`{:08X}", value >> 32, value & 0xFFFFFFFF) : std::format("{:08X}", value);
	}

	std::string W2U(std::wstring_view text) {
		return ToUtf8(text);
	}

	std::wstring Lower(std::wstring_view text) {
		std::wstring s(text);
		for (auto& c : s)
			c = static_cast<wchar_t>(std::towlower(c));
		return s;
	}

	// a whole match with * and ? (case-insensitive)
	bool WildMatch(std::wstring_view pattern, std::wstring_view text) {
		size_t p = 0, t = 0, star = std::wstring_view::npos, mark = 0;
		while (t < text.size()) {
			if (p < pattern.size() && (pattern[p] == L'?' || pattern[p] == text[t])) {
				p++;
				t++;
			}
			else if (p < pattern.size() && pattern[p] == L'*') {
				star = p++;
				mark = t;
			}
			else if (star != std::wstring_view::npos) {
				p = star + 1;
				t = ++mark;
			}
			else
				return false;
		}
		while (p < pattern.size() && pattern[p] == L'*')
			p++;
		return p == pattern.size();
	}

	// A filter: with * or ?, the whole text must match it; without, it must be in the text. Case-insensitive.
	class Filter {
	public:
		explicit Filter(std::wstring pattern) : m_Pattern(Lower(pattern)), m_Wild(m_Pattern.find_first_of(L"*?") != std::wstring::npos) {}
		bool Empty() const {
			return m_Pattern.empty();
		}
		bool Matches(std::wstring_view text) const {
			if (m_Pattern.empty())
				return true;
			auto lower = Lower(text);
			return m_Wild ? WildMatch(m_Pattern, lower) : lower.find(m_Pattern) != std::wstring::npos;
		}

	private:
		std::wstring m_Pattern;
		bool m_Wild;
	};

	std::string HrText(HRESULT hr) {
		if (hr == E_NOTIMPL)
			return "not supported by this debugger engine, or for this kind of target (E_NOTIMPL)";
		std::string text = std::format("error 0x{:08X}", static_cast<unsigned>(hr));
		wchar_t* message = nullptr;
		if (::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, hr, 0,
			reinterpret_cast<wchar_t*>(&message), 0, nullptr) && message) {
			std::wstring m(message);
			::LocalFree(message);
			while (!m.empty() && (m.back() == L'\n' || m.back() == L'\r' || m.back() == L' ' || m.back() == L'.'))
				m.pop_back();
			text += ": " + W2U(m);
		}
		return text;
	}

	ToolResult Error(std::string text) {
		return { std::move(text), true };
	}

	ToolResult Text(std::string text) {
		if (text.size() > MaxResultChars) {
			text.resize(MaxResultChars);
			text += "\n... (cut short: ask for less -- a filter, fewer frames or rows, a smaller size)";
		}
		return { std::move(text), false };
	}

	const char* StatusText(ExecutionStatus status) {
		switch (status) {
			case ExecutionStatus::Break: return "stopped";
			case ExecutionStatus::NoDebuggee: return "no target";
			case ExecutionStatus::Go:
			case ExecutionStatus::GoHandled:
			case ExecutionStatus::GoNotHandled: return "running";
			case ExecutionStatus::StepOver:
			case ExecutionStatus::StepInfo:
			case ExecutionStatus::StepBranch: return "running (stepping)";
			default: return "busy";
		}
	}

	const char* TargetKind(TargetType type) {
		if (type == TargetType::LocalKernel)
			return "the local kernel (this computer's, live: it never stops; nothing can be run or stepped)";
		if ((type & TargetType::DumpKernel) == TargetType::DumpKernel)
			return "a kernel dump";
		if ((type & TargetType::DumpUser) == TargetType::DumpUser)
			return "a user-mode dump";
		if ((type & TargetType::LiveKernel) == TargetType::LiveKernel)
			return "a live kernel (another computer, over a debugging connection)";
		if ((type & TargetType::LiveUser) == TargetType::LiveUser)
			return "a live user-mode process";
		return "unknown";
	}

	// the status, as the tools see it (a run in flight is running, whatever the status says yet)
	std::string StatusOf(const ToolContext& c) {
		if (c.RunsInFlight && *c.RunsInFlight > 0 && c.Host.Status() == ExecutionStatus::Break)
			return "running";
		return StatusText(c.Host.Status());
	}

	// a run (go, run_to, step) in flight while it lives (ToolContext::RunsInFlight)
	struct InFlight {
		explicit InFlight(int* count) : Count(count) {
			if (Count)
				++*Count;
		}
		~InFlight() {
			if (Count)
				--*Count;
		}
		InFlight(const InFlight&) = delete;
		InFlight& operator=(const InFlight&) = delete;
		int* Count;
	};

	bool IsKernel(ToolContext& c) {
		return (c.Dbg.GetTargetType() & TargetType::Kernel) == TargetType::Kernel;
	}

	// ---- arguments (a wrong type throws, and the call fails with what nlohmann says)

	bool Has(const json& args, const char* name) {
		return args.is_object() && args.contains(name) && !args[name].is_null();
	}

	int IntArg(const json& args, const char* name, int def, int min, int max) {
		if (!Has(args, name))
			return def;
		return std::clamp(args[name].get<int>(), min, max);
	}

	bool BoolArg(const json& args, const char* name, bool def) {
		return Has(args, name) ? args[name].get<bool>() : def;
	}

	std::wstring StrArg(const json& args, const char* name) {
		return Has(args, name) ? FromUtf8(args[name].get<std::string>()) : std::wstring();
	}

	// ---- state

	std::optional<ToolResult> RequireTarget(ToolContext& c) {
		if (!HasTarget(c.Host))
			return Error("No target: nothing is being debugged. The user starts a session in DebugX (Debug > Run Executable, "
				"Attach to Process, File > Open Dump File, Attach to Kernel).");
		return std::nullopt;
	}

	std::optional<ToolResult> RequireStopped(ToolContext& c) {
		if (auto error = RequireTarget(c))
			return error;
		if (!IsStopped(c))
			return Error(std::format("The target is {} -- it must be stopped for this. break_into stops it; wait_for_stop waits for it "
				"to stop on its own (a breakpoint, an exception).", StatusOf(c)));
		return std::nullopt;
	}

	// the target can be run or stepped: a live one (not the local kernel), stopped
	std::optional<ToolResult> RequireRunnable(ToolContext& c) {
		if (auto error = RequireStopped(c))
			return error;
		auto type = c.Dbg.GetTargetType();
		if ((type & TargetType::Dump) == TargetType::Dump)
			return Error("A dump cannot run: it is a snapshot.");
		if (type == TargetType::LocalKernel)
			return Error("The local kernel cannot be run, stepped or stopped: it is the running system.");
		return std::nullopt;
	}

	// an address: a number, or a debugger expression
	Task<HRESULT> ResolveAddress(ToolContext c, json value, ULONG64& address, std::string& error) {
		if (value.is_number_unsigned() || value.is_number_integer()) {
			address = value.get<ULONG64>();
			co_return S_OK;
		}
		if (!value.is_string()) {
			error = "an address is a number or an expression (a string)";
			co_return E_INVALIDARG;
		}
		auto expression = FromUtf8(value.get<std::string>());
		auto hr = co_await c.Dbg.Evaluate(expression.c_str(), address);
		if (FAILED(hr))
			error = std::format("'{}' does not evaluate to an address: {}", value.get<std::string>(), HrText(hr));
		co_return hr;
	}

	std::string SourceText(const SourceLocation& location) {
		if (location.File.empty())
			return {};
		return std::format("{}:{}", W2U(location.File), location.Line);
	}

	// the source locations of a stack's frames: frame 0 where it is, the others at their call (the return address minus one)
	Task<std::vector<SourceLocation>> FrameSources(ToolContext c, const std::vector<StackFrameInfo>& frames) {
		std::vector<ULONG64> offsets;
		for (auto& f : frames)
			offsets.push_back(f.Index == 0 || f.InstructionOffset == 0 ? f.InstructionOffset : f.InstructionOffset - 1);
		std::vector<SourceLocation> locations;
		if (FAILED(co_await c.Dbg.GetSourceLocations(std::move(offsets), locations)))
			locations.clear();
		locations.resize(frames.size());
		co_return locations;
	}

	std::string FrameLine(const StackFrameInfo& f, const SourceLocation* source, bool marked = false) {
		auto line = std::format("{}#{:02} {} {}", marked ? "> " : "  ", f.Index, Address(f.InstructionOffset),
			f.Symbol.empty() ? std::string("(no symbol)") : W2U(f.Symbol));
		if (source && !source->File.empty())
			line += "  [" + SourceText(*source) + "]";
		return line;
	}

	std::string ThreadLine(const ThreadInfo& t) {
		auto line = std::format("{} [{}] tid {} (0x{:X})", t.Current ? "*" : " ", t.Id, t.ThreadId, t.ThreadId);
		if (!t.Name.empty())
			line += " \"" + W2U(t.Name) + "\"";
		if (!t.Location.empty())
			line += " at " + W2U(t.Location);
		if (t.SuspendCount > 1)
			line += std::format(" (suspend count {})", t.SuspendCount);
		if (t.Frozen)
			line += " (frozen)";
		return line;
	}

	Task<HRESULT> GetThreads(ToolContext c, std::vector<ProcessInfo>& processes, std::vector<ThreadInfo>& threads) {
		co_return co_await c.Dbg.EnumProcesses(processes, threads);
	}

	// ---- thread / frame switching for the tools that are about "the current thread and frame"

	struct ScopeSwitch {
		bool ThreadChanged{ false };
		ULONG OldThread{ 0 };
		bool FrameChanged{ false };
		ULONG OldFrame{ 0 };
	};

	// Makes the thread_id and frame arguments the current thread and scope frame (LeaveScope puts them back); an error text
	// if they cannot be
	Task<std::string> EnterScope(ToolContext c, json args, ScopeSwitch& s) {
		ULONG64 ip = 0;
		ULONG frame = 0;
		co_await c.Dbg.GetScope(ip, frame);
		s.OldFrame = frame;
		if (Has(args, "thread_id")) {
			auto id = args["thread_id"].get<ULONG>();
			std::vector<ProcessInfo> processes;
			std::vector<ThreadInfo> threads;
			auto hr = co_await GetThreads(c, processes, threads);
			if (FAILED(hr))
				co_return "thread_id cannot be used with this target (" + HrText(hr) + "): leave it out for the current thread";
			auto it = std::find_if(threads.begin(), threads.end(), [&](auto& t) { return t.Id == id && processes.end() != std::find_if(processes.begin(),
				processes.end(), [&](auto& p) { return p.Current && p.Id == t.ProcessId; }); });
			if (it == threads.end())
				co_return std::format("there is no thread [{}] in the current process (list_threads has them; thread_id is the [N], not the tid)", id);
			auto current = std::find_if(threads.begin(), threads.end(), [](auto& t) { return t.Current; });
			if (current != threads.end() && current->Id != id) {
				hr = co_await c.Dbg.SetCurrentThread(id);
				if (FAILED(hr))
					co_return "cannot switch to thread " + std::to_string(id) + ": " + HrText(hr);
				s.ThreadChanged = true;
				s.OldThread = current->Id;
			}
		}
		if (Has(args, "frame")) {
			auto wanted = args["frame"].get<ULONG>();
			ULONG now = s.ThreadChanged ? 0 : frame;
			if (wanted != now) {
				auto hr = co_await c.Dbg.SetScopeFrame(wanted);
				if (FAILED(hr))
					co_return std::format("there is no frame {} ({})", wanted, HrText(hr));
				s.FrameChanged = true;
			}
		}
		co_return std::string();
	}

	Task<void> LeaveScope(ToolContext c, ScopeSwitch s) {
		if (s.ThreadChanged) {
			co_await c.Dbg.SetCurrentThread(s.OldThread);
			if (s.OldFrame)
				co_await c.Dbg.SetScopeFrame(s.OldFrame);
		}
		else if (s.FrameChanged)
			co_await c.Dbg.SetScopeFrame(s.OldFrame);
	}

	// ---- values (data model rows), expanded to a depth

	using SymbolQuery = std::function<Task<HRESULT>(std::vector<SymbolEntry>&, std::vector<std::wstring>)>;

	Task<HRESULT> QueryExpanded(SymbolQuery query, int depth, std::vector<SymbolEntry>& entries) {
		std::vector<std::wstring> expanded;
		HRESULT hr = S_OK;
		for (int level = 0; ; level++) {
			entries.clear();
			hr = co_await query(entries, expanded);
			if (FAILED(hr) || level >= depth)
				break;
			const auto before = expanded.size();
			for (auto& e : entries) {
				if (e.Expandable && !e.Expanded && static_cast<int>(e.Depth) == level && expanded.size() < 300)
					expanded.push_back(e.Path);
			}
			if (expanded.size() == before)
				break;
		}
		co_return hr;
	}

	std::string FormatEntries(const std::vector<SymbolEntry>& entries) {
		std::string out;
		constexpr size_t MaxRows = 600;
		size_t rows = 0;
		for (auto& e : entries) {
			if (++rows > MaxRows) {
				out += std::format("... ({} more rows: ask for less depth, or for a member by its expression)\n", entries.size() - MaxRows);
				break;
			}
			out += std::string(e.Depth * 2, ' ') + W2U(e.Name) + " = " + W2U(e.Value);
			if (!e.Type.empty())
				out += "  (" + W2U(e.Type) + ")";
			if (e.Expandable && !e.Expanded)
				out += " {...}";
			if (e.Error)
				out += "  [error]";
			out += "\n";
		}
		return out;
	}

	// ---- files

	std::wstring DecodeText(const std::string& bytes) {
		if (bytes.size() >= 2 && static_cast<BYTE>(bytes[0]) == 0xFF && static_cast<BYTE>(bytes[1]) == 0xFE)
			return std::wstring(reinterpret_cast<const wchar_t*>(bytes.data() + 2), (bytes.size() - 2) / 2);
		std::string_view text(bytes);
		if (text.size() >= 3 && text.substr(0, 3) == "\xEF\xBB\xBF")
			text.remove_prefix(3);
		if (text.empty())
			return {};
		for (UINT cp : { UINT(CP_UTF8), UINT(CP_ACP) }) {
			const DWORD flags = cp == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0;
			int n = ::MultiByteToWideChar(cp, flags, text.data(), static_cast<int>(text.size()), nullptr, 0);
			if (n > 0) {
				std::wstring result(n, L'\0');
				::MultiByteToWideChar(cp, flags, text.data(), static_cast<int>(text.size()), result.data(), n);
				return result;
			}
		}
		return {};
	}

	// ================================================================================================================
	// the tools
	// ================================================================================================================

	Task<ToolResult> GetSession(ToolContext c, json args) {
		auto type = c.Dbg.GetTargetType();
		if (type == TargetType::Unknown)
			co_return Text("No target: nothing is being debugged. The user starts a session in DebugX (Debug > Run Executable, "
				"Attach to Process, File > Open Dump File, Attach to Kernel).");
		std::string out;
		auto name = c.Host.TargetName();
		if (!name.empty())
			out += "Target: " + W2U(name) + "\n";
		out += std::string("Kind: ") + TargetKind(type) + "\n";
		out += "Status: " + StatusOf(c) + (c.Dbg.IsBusy() ? " (the debugger is busy with a request)" : "") + "\n";
		if (!IsStopped(c))
			co_return Text(out);

		if (!IsKernel(c)) {
			std::vector<ProcessInfo> processes;
			std::vector<ThreadInfo> threads;
			if (SUCCEEDED(co_await GetThreads(c, processes, threads))) {
				for (auto& p : processes)
					out += std::format("{}Process [{}] pid {} (0x{:X}) {}, {} threads\n", p.Current ? "* " : "  ", p.Id, p.ProcessId, p.ProcessId,
						W2U(p.ImagePath), p.ThreadCount);
				for (auto& t : threads)
					if (t.Current)
						out += "Current thread:" + ThreadLine(t).substr(1) + "\n";
			}
		}
		ULONG64 ip = 0;
		ULONG frame = 0;
		if (SUCCEEDED(co_await c.Dbg.GetScope(ip, frame)))
			out += std::format("Scope frame: {} (locals, registers and evaluate are about it)\n", frame);
		co_return Text(out);
	}

	Task<ToolResult> ListThreads(ToolContext c, json args) {
		if (auto error = RequireStopped(c))
			co_return *error;
		std::vector<ProcessInfo> processes;
		std::vector<ThreadInfo> threads;
		auto hr = co_await GetThreads(c, processes, threads);
		if (hr == E_NOTIMPL && IsKernel(c))
			co_return Error("Kernel targets have no thread list here: use run_command with !process 0 0 (processes), "
				"!process <addr> 7 (a process's threads with their stacks), !thread <addr>, or !running.");
		if (FAILED(hr))
			co_return Error("Cannot list the threads: " + HrText(hr));
		std::string out = std::format("{} thread(s); * is the current one. [N] is the thread's id for the other tools.\n", threads.size());
		for (auto& p : processes) {
			if (processes.size() > 1)
				out += std::format("{}Process [{}] pid {} {}\n", p.Current ? "* " : "  ", p.Id, p.ProcessId, W2U(p.ImagePath));
			for (auto& t : threads)
				if (t.ProcessId == p.Id)
					out += ThreadLine(t) + "\n";
		}
		co_return Text(out);
	}

	Task<ToolResult> GetCallStack(ToolContext c, json args) {
		if (auto error = RequireStopped(c))
			co_return *error;
		const auto max = IntArg(args, "max_frames", 64, 1, 1024);
		const bool source = BoolArg(args, "source", true);
		std::vector<StackFrameInfo> frames;
		HRESULT hr;
		if (Has(args, "thread_id"))
			hr = co_await c.Dbg.GetThreadStackTrace(args["thread_id"].get<ULONG>(), frames, max);
		else
			hr = co_await c.Dbg.GetStackTrace(frames, max);
		if (FAILED(hr))
			co_return Error("Cannot walk the stack: " + HrText(hr) + (Has(args, "thread_id") ? " (thread_id is the [N] of list_threads)" : ""));
		std::vector<SourceLocation> sources;
		if (source)
			sources = co_await FrameSources(c, frames);
		std::string out;
		for (size_t i = 0; i < frames.size(); i++)
			out += FrameLine(frames[i], source ? &sources[i] : nullptr) + "\n";
		if (frames.size() == static_cast<size_t>(max))
			out += std::format("(the first {} frames: max_frames asks for more)\n", max);
		co_return Text(out);
	}

	Task<ToolResult> GetAllCallStacks(ToolContext c, json args) {
		if (auto error = RequireStopped(c))
			co_return *error;
		const auto max = IntArg(args, "max_frames", 48, 1, 512);
		const bool group = BoolArg(args, "group_identical", true);
		const bool source = BoolArg(args, "source", false);
		Filter filter(StrArg(args, "filter"));

		std::vector<ProcessInfo> processes;
		std::vector<ThreadInfo> threads;
		auto hr = co_await GetThreads(c, processes, threads);
		if (hr == E_NOTIMPL && IsKernel(c))
			co_return Error("Kernel targets: use run_command with !process 0 7 (every thread of every process with its stack; long), "
				"!process 0 7 <image>.exe, or !stacks 2 <filter> (Windows Kits engine).");
		if (FAILED(hr))
			co_return Error("Cannot list the threads: " + HrText(hr));

		struct Stack {
			std::vector<StackFrameInfo> Frames;
			std::vector<SourceLocation> Sources;
			std::vector<const ThreadInfo*> Threads;
		};
		std::vector<Stack> stacks;
		std::map<std::wstring, size_t> byKey;
		size_t failed = 0, matched = 0;
		for (auto& t : threads) {
			std::vector<StackFrameInfo> frames;
			if (FAILED(co_await c.Dbg.GetThreadStackTrace(t.Id, frames, max))) {
				failed++;
				continue;
			}
			if (!filter.Empty() && std::none_of(frames.begin(), frames.end(), [&](auto& f) { return filter.Matches(f.Symbol); }))
				continue;
			matched++;
			std::wstring key;
			if (group) {
				key = std::to_wstring(t.ProcessId) + L"|";
				for (auto& f : frames)
					key += (f.Symbol.empty() ? std::to_wstring(f.InstructionOffset) : f.Symbol) + L"|";
				if (auto it = byKey.find(key); it != byKey.end()) {
					stacks[it->second].Threads.push_back(&t);
					continue;
				}
				byKey[key] = stacks.size();
			}
			Stack s;
			s.Frames = std::move(frames);
			s.Threads.push_back(&t);
			stacks.push_back(std::move(s));
		}
		if (source)
			for (auto& s : stacks)
				s.Sources = co_await FrameSources(c, s.Frames);

		std::string out = std::format("{} thread(s)", threads.size());
		if (!filter.Empty())
			out += std::format(", {} with a frame matching '{}' (marked >)", matched, args["filter"].get<std::string>());
		if (group)
			out += std::format(", {} distinct stack(s)", stacks.size());
		if (failed)
			out += std::format(", {} whose stack could not be walked", failed);
		out += ".\n";
		for (auto& s : stacks) {
			out += "\n--- ";
			if (s.Threads.size() > 1)
				out += std::format("{} threads: ", s.Threads.size());
			for (size_t i = 0; i < s.Threads.size(); i++) {
				auto t = s.Threads[i];
				if (i)
					out += ", ";
				if (i >= 24) {
					out += std::format("... ({} more)", s.Threads.size() - i);
					break;
				}
				out += std::format("[{}] tid 0x{:X}", t->Id, t->ThreadId);
				if (!t->Name.empty())
					out += " \"" + W2U(t->Name) + "\"";
				if (processes.size() > 1)
					out += std::format(" (process [{}])", t->ProcessId);
			}
			out += "\n";
			for (size_t i = 0; i < s.Frames.size(); i++)
				out += FrameLine(s.Frames[i], source ? &s.Sources[i] : nullptr, !filter.Empty() && filter.Matches(s.Frames[i].Symbol)) + "\n";
			if (s.Frames.size() == static_cast<size_t>(max))
				out += "  (more frames: max_frames)\n";
		}
		co_return Text(out);
	}

	Task<ToolResult> ListModules(ToolContext c, json args) {
		if (auto error = RequireStopped(c))
			co_return *error;
		Filter filter(StrArg(args, "filter"));
		std::vector<ModuleInfo> modules;
		auto hr = co_await c.Dbg.EnumModules(modules);
		if (FAILED(hr))
			co_return Error("Cannot list the modules: " + HrText(hr));
		static const char* const symbolKinds[] = { "none", "COFF", "CodeView", "PDB", "export symbols", "deferred", "SYM", "PDB" };
		std::sort(modules.begin(), modules.end(), [](auto& a, auto& b) { return a.BaseAddress < b.BaseAddress; });
		std::string out;
		size_t shown = 0;
		for (auto& m : modules) {
			if (!filter.Matches(m.Name) && !filter.Matches(m.ImageName))
				continue;
			shown++;
			out += std::format("{} {}-{} symbols: {}  {}", W2U(m.Name), Address(m.BaseAddress), Address(m.BaseAddress + m.Size),
				m.Symbols < std::size(symbolKinds) ? symbolKinds[m.Symbols] : "?", W2U(m.ImageName));
			if (!m.SymbolFile.empty() && m.Symbols != 0 && m.Symbols != 5)
				out += "  [" + W2U(m.SymbolFile) + "]";
			out += "\n";
		}
		co_return Text(std::format("{} of {} module(s)\n", shown, modules.size()) + out);
	}

	Task<ToolResult> GetRegisters(ToolContext c, json args) {
		if (auto error = RequireStopped(c))
			co_return *error;
		ScopeSwitch s;
		if (auto error = co_await EnterScope(c, args, s); !error.empty()) {
			co_await LeaveScope(c, s);
			co_return Error(error);
		}
		std::vector<RegisterInfo> registers;
		auto hr = co_await c.Dbg.GetRegisters(registers);
		co_await LeaveScope(c, s);
		if (FAILED(hr))
			co_return Error("Cannot read the registers: " + HrText(hr));

		Filter filter(StrArg(args, "filter"));
		const bool all = BoolArg(args, "all", false);
		auto isVector = [](const std::wstring& name) {
			static const wchar_t* const prefixes[] = { L"xmm", L"ymm", L"zmm", L"st", L"mm", L"fp", L"mxcsr", L"k" };
			for (auto p : prefixes)
				if (name.starts_with(p) && (name.size() == wcslen(p) || iswdigit(name[wcslen(p)]) || wcscmp(p, L"fp") == 0 || wcscmp(p, L"mxcsr") == 0))
					return true;
			return false;
		};
		// x86 / x64: the parts of the registers (eax of rax, ax, al...), the flags one by one, the debug registers -- what
		// the full registers already have
		const bool x64 = std::any_of(registers.begin(), registers.end(), [](auto& r) { return r.Name == L"rax"; });
		auto isPart = [x64](const std::wstring& name) {
			static const std::set<std::wstring> parts = {
				L"al", L"bl", L"cl", L"dl", L"ah", L"bh", L"ch", L"dh", L"spl", L"bpl", L"sil", L"dil",
				L"ax", L"bx", L"cx", L"dx", L"sp", L"bp", L"si", L"di", L"ip", L"fl",
				L"of", L"df", L"if", L"tf", L"sf", L"zf", L"af", L"pf", L"cf", L"vip", L"vif", L"iopl",
				L"dr0", L"dr1", L"dr2", L"dr3", L"dr6", L"dr7", L"exfrom", L"exto", L"brfrom", L"brto" };
			static const std::set<std::wstring> parts64 = { L"eax", L"ebx", L"ecx", L"edx", L"esp", L"ebp", L"esi", L"edi", L"eip" };
			if (parts.contains(name) || (x64 && parts64.contains(name)))
				return true;
			// r8d, r8w, r8b ... r15b
			return x64 && name.size() >= 3 && name[0] == L'r' && iswdigit(name[1]) && (name.back() == L'd' || name.back() == L'w' || name.back() == L'b');
		};
		std::string out;
		int column = 0;
		size_t hidden = 0;
		for (auto& r : registers) {
			if (!filter.Empty() ? !filter.Matches(r.Name) : (!all && (isVector(r.Name) || isPart(r.Name)))) {
				hidden++;
				continue;
			}
			out += W2U(r.Name) + "=" + W2U(r.Value);
			out += ++column % 4 == 0 ? "\n" : "  ";
		}
		if (column % 4)
			out += "\n";
		if (hidden && filter.Empty() && !all)
			out += std::format("({} more left out: parts of these (eax, ax, al), flags one by one, debug, floating-point and vector "
				"registers; all=true, or a filter such as \"xmm*\")\n", hidden);
		co_return Text(out);
	}

	Task<ToolResult> GetLocals(ToolContext c, json args) {
		if (auto error = RequireStopped(c))
			co_return *error;
		const int depth = IntArg(args, "depth", 1, 0, 4);
		ScopeSwitch s;
		if (auto error = co_await EnterScope(c, args, s); !error.empty()) {
			co_await LeaveScope(c, s);
			co_return Error(error);
		}
		std::vector<SymbolEntry> entries;
		auto& dbg = c.Dbg;
		auto hr = co_await QueryExpanded([&dbg](std::vector<SymbolEntry>& e, std::vector<std::wstring> expanded) {
			return dbg.GetLocals(e, std::move(expanded));
			}, depth, entries);
		ULONG64 ip = 0;
		ULONG frame = 0;
		co_await c.Dbg.GetScope(ip, frame);
		co_await LeaveScope(c, s);
		if (FAILED(hr))
			co_return Error("Cannot read the locals: " + HrText(hr));
		if (entries.empty())
			co_return Text(std::format("No arguments or locals in frame {} (no symbols with locals for its function, or it has none).\n", frame));
		co_return Text(std::format("Arguments and locals of frame {}:\n", frame) + FormatEntries(entries));
	}

	Task<ToolResult> Evaluate(ToolContext c, json args) {
		if (auto error = RequireStopped(c))
			co_return *error;
		std::vector<std::wstring> expressions;
		if (Has(args, "expressions"))
			for (auto& e : args["expressions"])
				expressions.push_back(FromUtf8(e.get<std::string>()));
		if (expressions.empty())
			co_return Error("expressions: one or more C++ expressions");
		const int depth = IntArg(args, "depth", 1, 0, 4);
		ScopeSwitch s;
		if (auto error = co_await EnterScope(c, args, s); !error.empty()) {
			co_await LeaveScope(c, s);
			co_return Error(error);
		}
		std::vector<SymbolEntry> entries;
		auto& dbg = c.Dbg;
		auto hr = co_await QueryExpanded([&dbg, expressions](std::vector<SymbolEntry>& e, std::vector<std::wstring> expanded) {
			return dbg.EvaluateWatches(e, expressions, std::move(expanded));
			}, depth, entries);
		co_await LeaveScope(c, s);
		if (FAILED(hr))
			co_return Error("Cannot evaluate: " + HrText(hr));
		co_return Text(FormatEntries(entries));
	}

	Task<ToolResult> ReadMemory(ToolContext c, json args) {
		if (auto error = RequireStopped(c))
			co_return *error;
		if (!Has(args, "address"))
			co_return Error("address is needed");
		ULONG64 address = 0;
		std::string error;
		if (FAILED(co_await ResolveAddress(c, args["address"], address, error)))
			co_return Error(error);
		const auto size = static_cast<ULONG>(IntArg(args, "size", 256, 1, 16384));
		const auto format = Has(args, "format") ? args["format"].get<std::string>() : std::string("bytes");
		std::vector<BYTE> data;
		auto hr = co_await c.Dbg.ReadMemory(address, data, size);
		if (FAILED(hr) || data.empty())
			co_return Error(std::format("Cannot read memory at {}: {}", Address(address), FAILED(hr) ? HrText(hr) : "not readable"));

		std::string out = std::format("{} bytes at {}{}:\n", data.size(), Address(address),
			data.size() < size ? std::format(" (only these could be read of {})", size) : "");
		if (format == "ascii" || format == "unicode") {
			std::wstring text;
			if (format == "ascii") {
				auto end = std::find(data.begin(), data.end(), BYTE(0));
				text = FromUtf8(std::string(data.begin(), end));
			}
			else {
				auto chars = reinterpret_cast<const wchar_t*>(data.data());
				size_t n = data.size() / 2, len = 0;
				while (len < n && chars[len])
					len++;
				text.assign(chars, len);
			}
			out += "\"" + W2U(text) + "\"\n";
			co_return Text(out);
		}
		size_t unit = format == "words" ? 2 : format == "dwords" ? 4 : format == "qwords" ? 8 : 1;
		const size_t perLine = 16;
		for (size_t offset = 0; offset < data.size(); offset += perLine) {
			out += Address(address + offset) + " ";
			const auto end = std::min(offset + perLine, data.size());
			for (size_t i = offset; i + unit <= end; i += unit) {
				ULONG64 v = 0;
				memcpy(&v, &data[i], unit);
				out += unit == 1 ? std::format(" {:02X}", v) : unit == 2 ? std::format(" {:04X}", v)
					: unit == 4 ? std::format(" {:08X}", v) : " " + Address(v);
			}
			if (unit == 1) {
				out += std::string((perLine - (end - offset)) * 3 + 2, ' ');
				for (size_t i = offset; i < end; i++)
					out += data[i] >= 0x20 && data[i] < 0x7F ? static_cast<char>(data[i]) : '.';
			}
			out += "\n";
		}
		co_return Text(out);
	}

	bool ParsePattern(std::string text, std::vector<BYTE>& bytes, std::string& error) {
		while (!text.empty() && isspace(static_cast<unsigned char>(text.back())))
			text.pop_back();
		while (!text.empty() && isspace(static_cast<unsigned char>(text.front())))
			text.erase(0, 1);
		if (text.size() >= 3 && text.starts_with("L\"") && text.back() == '"') {
			auto w = FromUtf8(text.substr(2, text.size() - 3));
			bytes.assign(reinterpret_cast<const BYTE*>(w.data()), reinterpret_cast<const BYTE*>(w.data() + w.size()));
		}
		else if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
			bytes.assign(text.begin() + 1, text.end() - 1);
		else {
			std::string digits;
			for (size_t i = 0; i < text.size(); i++) {
				if (text[i] == '0' && i + 1 < text.size() && (text[i + 1] == 'x' || text[i + 1] == 'X') && (i == 0 || !isxdigit(static_cast<unsigned char>(text[i - 1])))) {
					i++;
					continue;
				}
				if (isxdigit(static_cast<unsigned char>(text[i])))
					digits += text[i];
				else if (!isspace(static_cast<unsigned char>(text[i])) && text[i] != ',') {
					error = "pattern: hex bytes (\"48 8B 05\"), \"text\" or L\"text\"";
					return false;
				}
			}
			if (digits.size() % 2) {
				error = "pattern: an odd number of hex digits";
				return false;
			}
			for (size_t i = 0; i < digits.size(); i += 2)
				bytes.push_back(static_cast<BYTE>(std::stoul(digits.substr(i, 2), nullptr, 16)));
		}
		if (bytes.empty()) {
			error = "pattern is empty";
			return false;
		}
		return true;
	}

	Task<ToolResult> SearchMemory(ToolContext c, json args) {
		if (auto error = RequireStopped(c))
			co_return *error;
		std::vector<BYTE> pattern;
		std::string error;
		if (!Has(args, "pattern") || !ParsePattern(args["pattern"].get<std::string>(), pattern, error))
			co_return Error(error.empty() ? "pattern is needed" : error);
		ULONG64 start = 0;
		if (Has(args, "start")) {
			if (FAILED(co_await ResolveAddress(c, args["start"], start, error)))
				co_return Error(error);
		}
		else if (IsKernel(c))
			co_return Error("start is needed for a kernel target (the search covers 'length' bytes from it)");
		const auto maxResults = IntArg(args, "max_results", 10, 1, 200);
		ULONG64 length = 0x10000000;
		if (Has(args, "length"))
			length = args["length"].get<ULONG64>();

		std::vector<ULONG64> found;
		for (ULONG64 from = start; static_cast<int>(found.size()) < maxResults; ) {
			ULONG64 at = 0;
			auto hr = co_await c.Dbg.SearchMemory(from, pattern, at, IsKernel(c) ? (start + length > from ? start + length - from : 0) : length);
			if (FAILED(hr) || at < from)
				break;
			found.push_back(at);
			from = at + 1;
		}
		if (found.empty())
			co_return Text(std::format("Not found ({} bytes, from {}).", pattern.size(), Address(start)));
		std::string out = std::format("Found {} time(s){}:\n", found.size(), static_cast<int>(found.size()) == maxResults ? " (max_results: there may be more)" : "");
		for (auto a : found)
			out += Address(a) + "\n";
		co_return Text(out);
	}

	Task<ToolResult> Disassemble(ToolContext c, json args) {
		if (auto error = RequireStopped(c))
			co_return *error;
		ULONG64 ip = 0;
		ULONG frame = 0;
		co_await c.Dbg.GetScope(ip, frame);
		ULONG64 address = ip;
		std::string error;
		if (Has(args, "address") && FAILED(co_await ResolveAddress(c, args["address"], address, error)))
			co_return Error(error);
		const auto count = static_cast<ULONG>(IntArg(args, "count", 24, 1, 500));
		const bool source = BoolArg(args, "source", true);
		std::vector<DisassemblyLine> lines;
		auto hr = co_await c.Dbg.Disassemble(address, count, lines);
		if (FAILED(hr) || lines.empty())
			co_return Error(std::format("Cannot disassemble at {}: {}", Address(address), FAILED(hr) ? HrText(hr) : "not readable"));
		std::vector<SourceLocation> sources;
		if (source) {
			std::vector<ULONG64> offsets;
			for (auto& l : lines)
				offsets.push_back(l.Address);
			if (FAILED(co_await c.Dbg.GetSourceLocations(std::move(offsets), sources)))
				sources.clear();
			sources.resize(lines.size());
		}
		std::string out;
		std::string lastSource;
		for (size_t i = 0; i < lines.size(); i++) {
			if (source) {
				auto s = SourceText(sources[i]);
				if (!s.empty() && s != lastSource)
					out += "; " + s + "\n";
				lastSource = s;
			}
			out += (lines[i].Address == ip ? "=> " : "   ") + W2U(lines[i].Text);
			if (out.empty() || out.back() != '\n')
				out += "\n";
		}
		co_return Text(out);
	}

	Task<ToolResult> GetSource(ToolContext c, json args) {
		if (auto error = RequireStopped(c))
			co_return *error;
		const int context = IntArg(args, "context", 15, 0, 300);
		ULONG64 address = 0;
		std::string error;
		ULONG focus = 0;
		if (Has(args, "address")) {
			if (FAILED(co_await ResolveAddress(c, args["address"], address, error)))
				co_return Error(error);
		}
		else if (Has(args, "file") && Has(args, "line")) {
			// (only a file the symbols know of: this is not a way to read any file of the machine -- DebugX may be elevated)
			focus = args["line"].get<ULONG>();
			auto location = c.Dbg.SourceLineLocation(StrArg(args, "file"), focus);
			auto hr = co_await c.Dbg.ResolveLocation(location.c_str(), address);
			if (FAILED(hr))
				co_return Error(std::format("The symbols know of no code at {}:{} ({}). Give the file as the symbols name it (a call "
					"stack's [file:line]), or an address.", args["file"].get<std::string>(), focus, HrText(hr)));
		}
		else
			co_return Error("address, or file and line, is needed");

		std::vector<SourceLocation> locations;
		auto hr = co_await c.Dbg.GetSourceLocations({ address }, locations);
		if (FAILED(hr) || locations.empty() || locations[0].File.empty())
			co_return Error(std::format("No line information for {}: no symbols with lines for it", Address(address)));
		auto& loc = locations[0];
		if (!focus)
			focus = loc.Line;
		if (loc.Path.empty())
			co_return Error(std::format("{}:{} -- the file is not on this computer (DebugX looks for it through Options > Symbol and Source "
				"Paths, and where it was compiled)", W2U(loc.File), focus));

		std::ifstream file(loc.Path, std::ios::binary);
		if (!file)
			co_return Error("Cannot open " + W2U(loc.Path));
		std::stringstream buffer;
		buffer << file.rdbuf();
		auto text = DecodeText(buffer.str());
		std::vector<std::wstring_view> lines;
		for (size_t pos = 0; pos <= text.size(); ) {
			auto end = text.find(L'\n', pos);
			if (end == std::wstring::npos)
				end = text.size();
			auto line = std::wstring_view(text).substr(pos, end - pos);
			if (!line.empty() && line.back() == L'\r')
				line.remove_suffix(1);
			lines.push_back(line);
			pos = end + 1;
		}
		const ULONG first = focus > static_cast<ULONG>(context) ? focus - context : 1;
		const ULONG last = std::min<ULONG>(focus + context, static_cast<ULONG>(lines.size()));
		std::string out = W2U(loc.Path) + "\n";
		for (ULONG n = first; n <= last; n++)
			out += std::format("{}{:5}  {}\n", n == focus ? ">" : " ", n, W2U(lines[n - 1]));
		co_return Text(out);
	}

	std::string BreakpointLine(const BreakpointInfo& bp) {
		std::string line = std::format("[{}] {} ", bp.Id, bp.Enabled ? "enabled " : "disabled");
		if (bp.IsData)
			line += std::format("data ({} byte(s), on {}) at {}", bp.DataSize,
				bp.DataAccess == BreakOnRead ? "read/write" : bp.DataAccess == BreakOnExecute ? "execute" : "write", W2U(bp.Expression));
		else
			line += W2U(bp.Location.empty() ? bp.Expression : bp.Location);
		line += bp.Deferred ? " (deferred: its module is not loaded)" : " = " + Address(bp.Offset);
		if (!bp.Condition.empty())
			line += " if " + W2U(bp.Condition);
		if (bp.PassCount > 1)
			line += std::format(" (from hit {})", bp.PassCount);
		if (bp.OneShot)
			line += " (one-shot)";
		if (bp.ThreadId != AnyId)
			line += std::format(" (thread [{}] only)", bp.ThreadId);
		if (!bp.Command.empty())
			line += " command: " + W2U(bp.Command);
		line += std::format(", hits: {}", bp.Hits);
		return line;
	}

	Task<ToolResult> ListBreakpoints(ToolContext c, json args) {
		if (auto error = RequireTarget(c))
			co_return *error;
		if (!IsStopped(c))
			co_return Error("The target is running: the breakpoints can be listed once it stops.");
		std::vector<BreakpointInfo> bps;
		auto hr = co_await c.Dbg.EnumBreakpoints(bps);
		if (FAILED(hr))
			co_return Error("Cannot list the breakpoints: " + HrText(hr));
		if (bps.empty())
			co_return Text("No breakpoints.");
		std::string out;
		for (auto& bp : bps)
			out += BreakpointLine(bp) + "\n";
		co_return Text(out);
	}

	Task<ToolResult> SetBreakpoint(ToolContext c, json args) {
		if (auto error = RequireStopped(c))
			co_return *error;
		BreakpointSettings settings;
		if (Has(args, "file") && Has(args, "line"))
			settings.Expression = c.Dbg.SourceLineLocation(StrArg(args, "file"), args["line"].get<ULONG>());
		else
			settings.Expression = StrArg(args, "location");
		if (settings.Expression.empty())
			co_return Error("location (an address, module!function, ...) or file and line is needed");
		settings.Condition = StrArg(args, "condition");
		settings.OneShot = BoolArg(args, "one_shot", false);
		if (Has(args, "data_size")) {
			settings.IsData = true;
			settings.DataSize = args["data_size"].get<ULONG>();
			auto access = Has(args, "data_access") ? args["data_access"].get<std::string>() : std::string("write");
			settings.DataAccess = access == "read" ? BreakOnRead : access == "execute" ? BreakOnExecute : BreakOnWrite;
		}
		std::vector<BreakpointInfo> before;
		co_await c.Dbg.EnumBreakpoints(before);
		auto hr = co_await c.Dbg.AddBreakpoint(settings);
		if (FAILED(hr))
			co_return Error("Cannot set the breakpoint: " + HrText(hr));
		std::vector<BreakpointInfo> after;
		co_await c.Dbg.EnumBreakpoints(after);
		for (auto& bp : after)
			if (std::none_of(before.begin(), before.end(), [&](auto& b) { return b.Id == bp.Id; }))
				co_return Text("Set: " + BreakpointLine(bp));
		co_return Text("Set.");
	}

	Task<ToolResult> RemoveBreakpoint(ToolContext c, json args) {
		if (auto error = RequireStopped(c))
			co_return *error;
		if (!Has(args, "id"))
			co_return Error("id is needed (list_breakpoints has them)");
		auto hr = co_await c.Dbg.RemoveBreakpoint(args["id"].get<ULONG>());
		if (FAILED(hr))
			co_return Error("Cannot remove it: " + HrText(hr));
		co_return Text("Removed.");
	}

	Task<ToolResult> ListHandles(ToolContext c, json args) {
		if (auto error = RequireStopped(c))
			co_return *error;
		Filter filter(StrArg(args, "filter"));
		std::vector<HandleInfo> handles;
		auto hr = co_await c.Dbg.EnumHandles(handles);
		if (FAILED(hr))
			co_return Error("Cannot list the handles: " + HrText(hr) + " (a live user-mode process on this computer only; run_command !handle works in a dump with handle data)");
		std::string out;
		size_t shown = 0;
		for (auto& h : handles) {
			if (!filter.Matches(h.Type + L" " + h.Name))
				continue;
			shown++;
			out += std::format("0x{:X} {} {} access 0x{:X}\n", h.Handle, W2U(h.Type), h.Name.empty() ? std::string("-") : W2U(h.Name), h.GrantedAccess);
		}
		co_return Text(std::format("{} of {} handle(s)\n", shown, handles.size()) + out);
	}

	Task<ToolResult> Analyze(ToolContext c, json args) {
		if (auto error = RequireStopped(c))
			co_return *error;
		std::wstring text;
		auto hr = co_await c.Dbg.Analyze(text);
		if (FAILED(hr))
			co_return Error("Cannot analyze: " + HrText(hr) + (hr == E_NOTIMPL ? " -- !analyze needs the Windows Kits debugger's engine" : ""));
		co_return Text(W2U(text));
	}

	Task<ToolResult> RunCommand(ToolContext c, json args) {
		auto command = StrArg(args, "command");
		if (command.empty())
			co_return Error("command is needed");
		if (HasTarget(c.Host) && !IsStopped(c))
			co_return Error(std::format("The target is {}: commands run once it stops (break_into, wait_for_stop).", StatusOf(c)));
		std::wstring output;
		auto hr = co_await c.Dbg.ExecuteCommandCaptured(command.c_str(), output);
		if (FAILED(hr))
			co_return Error("Cannot run it: " + HrText(hr));
		co_return Text(output.empty() ? std::string("(no output)") : W2U(output));
	}

	// ---- running

	std::optional<ToolResult> Failed(HRESULT hr, const char* what) {
		if (FAILED(hr))
			return Error(std::string(what) + ": " + HrText(hr));
		return std::nullopt;
	}

	Task<ToolResult> Go(ToolContext c, json args) {
		if (auto error = RequireRunnable(c))
			co_return *error;
		InFlight running(c.RunsInFlight);
		auto hr = co_await c.Dbg.Go();		// (completes when the target stops again)
		if (auto error = Failed(hr, "Cannot run the target"))
			co_return *error;
		co_return co_await StopSummary(c, args);
	}

	Task<ToolResult> RunTo(ToolContext c, json args) {
		if (auto error = RequireRunnable(c))
			co_return *error;
		std::wstring location;
		if (Has(args, "file") && Has(args, "line"))
			location = c.Dbg.SourceLineLocation(StrArg(args, "file"), args["line"].get<ULONG>());
		else if (Has(args, "location")) {
			ULONG64 address = 0;
			std::string error;
			if (FAILED(co_await ResolveAddress(c, args["location"], address, error)))
				co_return Error(error);
			location = c.Dbg.AddressLocation(address);
		}
		else
			co_return Error("location (an address or expression), or file and line, is needed");
		InFlight running(c.RunsInFlight);
		auto hr = co_await c.Dbg.RunTo(location.c_str());
		if (auto error = Failed(hr, "Cannot run the target"))
			co_return *error;
		co_return co_await StopSummary(c, args);
	}

	Task<ToolResult> Step(ToolContext c, json args) {
		if (auto error = RequireRunnable(c))
			co_return *error;
		auto kind = Has(args, "kind") ? args["kind"].get<std::string>() : std::string("over");
		StepKind step = kind == "into" ? StepKind::Into : kind == "out" ? StepKind::Out : StepKind::Over;
		InFlight running(c.RunsInFlight);
		auto hr = co_await c.Dbg.Step(step, BoolArg(args, "by_source_line", true));
		if (auto error = Failed(hr, "Cannot step"))
			co_return *error;
		co_return co_await StopSummary(c, args);
	}

	Task<ToolResult> BreakInto(ToolContext c, json args) {
		if (auto error = RequireTarget(c))
			co_return *error;
		if (IsStopped(c))
			co_return Text("The target is already stopped.");
		auto hr = c.Dbg.Break();
		if (auto error = Failed(hr, "Cannot break in"))
			co_return *error;
		co_return Text("");
	}

	Task<ToolResult> WaitForStop(ToolContext c, json args) {
		if (auto error = RequireTarget(c))
			co_return *error;
		co_return Text("");
	}

	// ---- the schemas

	json Schema(json properties = json::object(), std::vector<std::string> required = {}) {
		json s = { { "type", "object" }, { "properties", std::move(properties) }, { "additionalProperties", false } };
		if (!required.empty())
			s["required"] = required;
		return s;
	}

	json Prop(const char* type, const char* description) {
		return { { "type", type }, { "description", description } };
	}

	json IntProp(const char* description, int min, int max) {
		return { { "type", "integer" }, { "description", description }, { "minimum", min }, { "maximum", max } };
	}

	json EnumProp(const char* description, std::vector<std::string> values) {
		return { { "type", "string" }, { "description", description }, { "enum", values } };
	}

	json AddressProp(const char* description) {
		return { { "type", { "string", "integer" } }, { "description", description } };
	}

	const json ThreadIdProp = IntProp("The debugger's id of the thread ([N] in list_threads; not the system's tid). Default: the current thread.", 0, 1000000);
	const json FrameProp = IntProp("The frame of the thread's stack (#N in get_call_stack). Default: the current scope frame (0 after a stop).", 0, 10000);
}

bool HasTarget(IMcpHost& host) {
	return host.Debugger().GetTargetType() != TargetType::Unknown;
}

namespace {
	// as far as the status says
	bool StatusIsStopped(IMcpHost& host) {
		return HasTarget(host) && (host.Status() == ExecutionStatus::Break || host.Debugger().GetTargetType() == TargetType::LocalKernel);
	}
}

bool IsStopped(const ToolContext& context) {
	return StatusIsStopped(context.Host) && (!context.RunsInFlight || *context.RunsInFlight == 0);
}

std::string SessionLine(const ToolContext& c) {
	auto type = c.Dbg.GetTargetType();
	if (type == TargetType::Unknown)
		return "no target: nothing is being debugged";
	auto name = c.Host.TargetName();
	return (name.empty() ? std::string("a target") : W2U(name)) + " (" + TargetKind(type) + "), " + StatusOf(c);
}

Task<ToolResult> StopSummary(ToolContext c, json args) {
	if (!HasTarget(c.Host))
		co_return Text("The session has ended: there is no target any more.");
	if (!StatusIsStopped(c.Host))
		co_return Text(std::format("Status: {}.", StatusText(c.Host.Status())));
	std::string out = "Status: stopped.\n";
	// (DbgEng's: why the target stopped; another engine fails it, and there is no line)
	std::wstring last;
	if (SUCCEEDED(co_await c.Dbg.ExecuteCommandCaptured(L".lastevent", last))) {
		std::wistringstream lines(last);
		std::wstring line;
		while (std::getline(lines, line)) {
			if (line.find(L"debugger time") != std::wstring::npos)
				continue;
			while (!line.empty() && (line.back() == L'\r' || line.back() == L' '))
				line.pop_back();
			if (!line.empty())
				out += W2U(line) + "\n";
		}
	}
	std::vector<StackFrameInfo> frames;
	if (SUCCEEDED(co_await c.Dbg.GetStackTrace(frames, 6))) {
		auto sources = co_await FrameSources(c, frames);
		out += "Current thread's stack (top):\n";
		for (size_t i = 0; i < frames.size(); i++)
			out += FrameLine(frames[i], &sources[i]) + "\n";
	}
	co_return Text(out);
}

const std::vector<ToolDef>& Tools() {
	static const std::vector<ToolDef> tools = {
		{ "get_session", "Debugging session",
			"What is being debugged (a process, a dump, a kernel), whether it is stopped, its processes and current thread. Call this first.",
			Schema(), true, false, ToolKind::Serialized, 30, nullptr, 0, false, GetSession },
		{ "list_threads", "Threads",
			"The threads of the target (user mode): the debugger's id [N] (what the other tools take as thread_id), system tid, name, "
			"and where each is (module!function+offset). * marks the current thread.",
			Schema(), true, false, ToolKind::Serialized, 60, nullptr, 0, false, ListThreads },
		{ "get_call_stack", "Call stack",
			"The call stack of one thread (default: the current one), innermost frame (#00) first, with source file and line where "
			"there are symbols. Does not change the current thread.",
			Schema({ { "thread_id", ThreadIdProp }, { "max_frames", IntProp("At most this many frames (default 64).", 1, 1024) },
				{ "source", Prop("boolean", "Add each frame's source file and line (default true).") } }),
			true, false, ToolKind::Serialized, 60, nullptr, 0, false, GetCallStack },
		{ "get_all_call_stacks", "All call stacks",
			"The call stacks of all threads (user mode), threads with the same stack grouped together. With a filter, only the threads "
			"that have a frame matching it, with those frames marked: e.g. \"mymodule!*\" for the threads running in that module, "
			"\"*!*Critical*\" or \"*Wait*\". The overview of what a process is doing (deadlocks, hangs, busy threads).",
			Schema({ { "filter", Prop("string", "Keep threads with a frame whose module!function matches: * and ? wildcards (the whole name), "
					"or else a substring. Case-insensitive.") },
				{ "max_frames", IntProp("Frames per thread (default 48).", 1, 512) },
				{ "group_identical", Prop("boolean", "Show threads with identical stacks once (default true).") },
				{ "source", Prop("boolean", "Add source file and line to the frames (default false).") } }),
			true, false, ToolKind::Serialized, 180, nullptr, 0, false, GetAllCallStacks },
		{ "list_modules", "Modules",
			"The loaded modules (DLLs, the executable; drivers for a kernel): name, address range, what symbols are loaded, image path.",
			Schema({ { "filter", Prop("string", "Only modules whose name matches (wildcards or a substring).") } }),
			true, false, ToolKind::Serialized, 60, nullptr, 0, false, ListModules },
		{ "get_registers", "Registers",
			"The registers of a thread at a frame (default: the current thread and scope frame; in a frame other than 0 only the "
			"nonvolatile ones are reliable). Floating-point and vector registers are left out unless asked for.",
			Schema({ { "thread_id", ThreadIdProp }, { "frame", FrameProp },
				{ "filter", Prop("string", "Only registers whose name matches (\"r*\", \"xmm0\", \"eflags\").") },
				{ "all", Prop("boolean", "Include floating-point and vector registers too.") } }),
			true, false, ToolKind::Serialized, 60, nullptr, 0, false, GetRegisters },
		{ "get_locals", "Locals",
			"The arguments and local variables of a frame (default: the current thread's scope frame), with their members to the depth "
			"asked, as the debugger's data model shows them (dx). Needs symbols with locals (a PDB with private symbols).",
			Schema({ { "thread_id", ThreadIdProp }, { "frame", FrameProp },
				{ "depth", IntProp("How deep to expand structures, arrays, pointers (0: just the variables; default 1).", 0, 4) } }),
			true, false, ToolKind::Serialized, 90, nullptr, 0, false, GetLocals },
		{ "evaluate", "Evaluate",
			"Evaluates C++ expressions in a frame's scope with the debugger's data model (as dx): variables, members (a.b, p->c), "
			"array elements, casts ((MyType*)0x1234), function-like data model queries (@$curprocess.Threads.Count()). Each result is "
			"expanded to the depth asked. An expression that does not evaluate says why.",
			Schema({ { "expressions", { { "type", "array" }, { "items", { { "type", "string" } } }, { "minItems", 1 },
					{ "description", "C++ / data model expressions." } } },
				{ "thread_id", ThreadIdProp }, { "frame", FrameProp },
				{ "depth", IntProp("How deep to expand each result (default 1).", 0, 4) } }, { "expressions" }),
			true, false, ToolKind::Serialized, 90, nullptr, 0, false, Evaluate },
		{ "read_memory", "Read memory",
			"Reads target memory: a hex dump with ASCII (bytes), words, dwords, qwords, or a NUL-terminated string (ascii: UTF-8, "
			"unicode: UTF-16). For pointers with their symbols use run_command with dps <address>.",
			Schema({ { "address", AddressProp("A number or an expression: 0x7ff612340000, @rsp, poi(@rcx+8), module!symbol.") },
				{ "size", IntProp("Bytes to read (default 256).", 1, 16384) },
				{ "format", EnumProp("How to show it (default bytes).", { "bytes", "words", "dwords", "qwords", "ascii", "unicode" }) } },
				{ "address" }),
			true, false, ToolKind::Serialized, 60, nullptr, 0, false, ReadMemory },
		{ "search_memory", "Search memory",
			"Finds bytes in target memory: in user mode in the committed memory of the current process from start (default: all of "
			"it), in kernel mode in length bytes from start.",
			Schema({ { "pattern", Prop("string", "Hex bytes (\"48 8B 05\"), \"ASCII or UTF-8 text\" in quotes, or L\"UTF-16 text\".") },
				{ "start", AddressProp("Where to start (needed for a kernel target).") },
				{ "length", Prop("integer", "Kernel targets: how many bytes from start (default 0x10000000).") },
				{ "max_results", IntProp("Stop after this many (default 10).", 1, 200) } }, { "pattern" }),
			true, false, ToolKind::Serialized, 120, nullptr, 0, false, SearchMemory },
		{ "disassemble", "Disassemble",
			"Disassembles instructions from an address (default: where the current scope frame is executing, marked =>), with "
			"source lines in between. To see instructions before an address use run_command with ub <address>.",
			Schema({ { "address", AddressProp("Where to start: a number or an expression (module!function, @rip).") },
				{ "count", IntProp("Instructions (default 24).", 1, 500) },
				{ "source", Prop("boolean", "Show the source file and line before their instructions (default true).") } }),
			true, false, ToolKind::Serialized, 60, nullptr, 0, false, Disassemble },
		{ "get_source", "Source code",
			"Source code around a line: the line of an address (a frame's instruction), or a file and line as the symbols name them "
			"(as get_call_stack shows them). Only files the symbols refer to, found on this computer.",
			Schema({ { "address", AddressProp("An address (or expression) in code with line information.") },
				{ "file", Prop("string", "A source file as the symbols name it, with line.") },
				{ "line", IntProp("The line in file.", 1, 10000000) },
				{ "context", IntProp("Lines before and after (default 15).", 0, 300) } }),
			true, false, ToolKind::Serialized, 60, nullptr, 0, false, GetSource },
		{ "list_breakpoints", "Breakpoints",
			"The breakpoints of the session: id, where, enabled, condition, hits.",
			Schema(), true, false, ToolKind::Serialized, 30, nullptr, 0, false, ListBreakpoints },
		{ "list_handles", "Handles",
			"The handles of the current process (a live user-mode process on this computer): value, type, name, granted access.",
			Schema({ { "filter", Prop("string", "Only handles whose type or name matches (\"Mutant\", \"*\\\\Registry\\\\*\").") } }),
			true, false, ToolKind::Serialized, 60, nullptr, 0, false, ListHandles },
		{ "analyze", "Analyze (!analyze -v)",
			"The debugger's automatic analysis of a crash, hang or break (!analyze -v): the faulting code, exception, bucket, "
			"likely culprit. Can take minutes the first time (symbols are downloaded). Needs the Windows Kits debugger's engine.",
			Schema(), true, false, ToolKind::Serialized, 600, nullptr, 0, false, Analyze },
		{ "run_command", "Debugger command",
			"Runs a debugger command (WinDbg syntax) and returns its output: dt, dx, dps, ub, !heap, !locks, !peb, !teb, !handle, "
			"!process, !thread, !irp, !pool, lm, .lastevent, .exr -1, .ecxr, ... A command can change the session (breakpoints, "
			"memory, registers); do not resume the target with it (g, p, t): use go, step, run_to.",
			Schema({ { "command", Prop("string", "The command, as typed at the debugger's prompt.") } }, { "command" }),
			false, true, ToolKind::Serialized, 180, nullptr, 0, false, RunCommand },
		{ "set_breakpoint", "Set breakpoint",
			"Sets a breakpoint: on code (an address, module!function, or a source file and line) or on data (data_size: the "
			"debugger breaks when the memory is written, or read). Optional condition, a C++ expression evaluated in the thread "
			"that hits it.",
			Schema({ { "location", Prop("string", "Code: an address or expression (module!function, 0x7ff6...); data: the address.") },
				{ "file", Prop("string", "A source file (as the symbols name it), with line, instead of location.") },
				{ "line", IntProp("The line in file.", 1, 10000000) },
				{ "condition", Prop("string", "Break only when this C++ expression is nonzero.") },
				{ "one_shot", Prop("boolean", "Remove it once it is hit.") },
				{ "data_size", { { "type", "integer" }, { "enum", { 1, 2, 4, 8 } }, { "description", "A data breakpoint, on this many bytes (aligned to the size)." } } },
				{ "data_access", EnumProp("Data breakpoints: write (default), read (read or write), execute.", { "write", "read", "execute" }) } }),
			false, false, ToolKind::Serialized, 60, nullptr, 0, false, SetBreakpoint },
		{ "remove_breakpoint", "Remove breakpoint",
			"Removes a breakpoint by its id (list_breakpoints).",
			Schema({ { "id", IntProp("The breakpoint's id.", 0, 1000000) } }, { "id" }),
			false, true, ToolKind::Serialized, 30, nullptr, 0, false, RemoveBreakpoint },
		{ "go", "Go (continue)",
			"Lets the target run until something stops it (a breakpoint, an exception, break_into), and says why it stopped and "
			"where. If it has not stopped within wait_seconds, it says so and the target keeps running.",
			Schema({ { "wait_seconds", IntProp("How long to wait for it to stop (default 10).", 0, 600) } }),
			false, false, ToolKind::Immediate, 10, "wait_seconds", 600, true, Go },
		{ "run_to", "Run to",
			"Lets the current thread run to a location (an address, module!function, or a source file and line) and stop there -- "
			"or wherever something else stops it first.",
			Schema({ { "location", AddressProp("An address or expression.") },
				{ "file", Prop("string", "A source file (as the symbols name it), with line, instead of location.") },
				{ "line", IntProp("The line in file.", 1, 10000000) },
				{ "wait_seconds", IntProp("How long to wait for it to stop (default 10).", 0, 600) } }),
			false, false, ToolKind::Immediate, 10, "wait_seconds", 600, true, RunTo },
		{ "step", "Step",
			"Steps the current thread: over a call, into it, or out of the function; by source line (default) or by instruction. "
			"Says where it is then.",
			Schema({ { "kind", EnumProp("over (default), into, out.", { "over", "into", "out" }) },
				{ "by_source_line", Prop("boolean", "By source line where there is line information (default true); false: one instruction.") } }),
			false, false, ToolKind::Immediate, 30, nullptr, 0, true, Step },
		{ "break_into", "Break",
			"Stops (pauses) the running target, and says where it is.",
			Schema(), false, false, ToolKind::WaitForStop, 15, nullptr, 0, true, BreakInto },
		{ "wait_for_stop", "Wait for stop",
			"Waits for the running target to stop on its own (a breakpoint, an exception), and says why and where.",
			Schema({ { "timeout_seconds", IntProp("How long to wait (default 30).", 0, 600) } }),
			true, false, ToolKind::WaitForStop, 30, "timeout_seconds", 600, true, WaitForStop },
	};
	return tools;
}

const ToolDef* FindTool(std::string_view name) {
	for (auto& t : Tools())
		if (name == t.Name)
			return &t;
	return nullptr;
}

std::string ToUtf8(std::wstring_view text) {
	if (text.empty())
		return {};
	int n = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
	std::string result(n, '\0');
	::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), n, nullptr, nullptr);
	return result;
}

std::wstring FromUtf8(std::string_view text) {
	if (text.empty())
		return {};
	int n = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
	std::wstring result(n, L'\0');
	::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), n);
	return result;
}

}
