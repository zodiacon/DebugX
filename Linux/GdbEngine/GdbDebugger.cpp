#include "GdbDebugger.h"
#include "Utf8.h"
#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <signal.h>
#include <sstream>
#include <string.h>
#include <unistd.h>

using namespace DebugEngine;
namespace fs = std::filesystem;

namespace GdbEngine {
	namespace {
		std::string Basename(const std::string& path) {
			auto pos = path.find_last_of('/');
			return pos == std::string::npos ? path : path.substr(pos + 1);
		}


		std::string ToLower(std::string s) {
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		bool Contains(const std::string& text, std::string_view what) {
			return ToLower(text).find(what) != std::string::npos;
		}

		// a breakpoint number ("3", or "3.1" for a location)
		ULONG BreakpointNumber(const std::string& text) {
			return static_cast<ULONG>(ParseNumber(text, 0, 10));
		}

		// a host signal's number by GDB's name for it (SIGSEGV, SIG34); 0 if the host has no such signal
		int SignalNumber(const std::string& name) {
			if (name.rfind("SIG", 0) != 0)
				return 0;
			auto rest = name.substr(3);
			if (!rest.empty() && std::isdigit(static_cast<unsigned char>(rest[0]))) {
				int n = std::atoi(rest.c_str());
				return n >= SIGRTMIN && n <= SIGRTMAX ? n : 0;
			}
			for (int i = 1; i < NSIG; i++) {
				auto abbrev = ::sigabbrev_np(i);
				if (abbrev && rest == abbrev)
					return i;
			}
			return 0;
		}

		std::string SignalName(int number) {
			if (auto abbrev = ::sigabbrev_np(number))
				return std::string("SIG") + abbrev;
			return std::format("SIG{}", number);
		}

		// the regions of a process's memory (/proc/<pid>/maps)
		struct Region {
			ULONG64 Start, End;
			bool Readable;
			std::string Path;
		};

		std::vector<Region> ReadMaps(int pid) {
			std::vector<Region> regions;
			std::ifstream maps(std::format("/proc/{}/maps", pid));
			std::string line;
			while (std::getline(maps, line)) {
				Region r{};
				char perms[8]{};
				unsigned long long start, end;
				int pathPos = 0;
				if (std::sscanf(line.c_str(), "%llx-%llx %7s %*s %*s %*s %n", &start, &end, perms, &pathPos) < 3)
					continue;
				r.Start = start;
				r.End = end;
				r.Readable = perms[0] == 'r';
				if (pathPos > 0 && pathPos < static_cast<int>(line.size()))
					r.Path = line.substr(pathPos);
				regions.push_back(std::move(r));
			}
			return regions;
		}

		std::vector<std::string> SplitPaths(const std::wstring& paths) {
			std::vector<std::string> result;
			std::string current;
			for (char c : ToUtf8(paths)) {
				if (c == ':' || c == ';' || c == '\n' || c == '\r') {
					if (!current.empty())
						result.push_back(std::move(current));
					current.clear();
				}
				else
					current += c;
			}
			if (!current.empty())
				result.push_back(std::move(current));
			return result;
		}
	}

	//
	// Awaiters
	//

	// a command: suspends until its result record has been handled
	class GdbDebugger::CommandAwaiter {
	public:
		CommandAwaiter(GdbDebugger* self, std::string command, int flags) : m_Self(self), m_Command(std::move(command)), m_Flags(flags) {}

		bool await_ready() const noexcept {
			return false;
		}
		bool await_suspend(std::coroutine_handle<> handle) {
			if (m_Self->Send(std::move(m_Command), m_Flags, [this, handle](MiResult&& result) {
				m_Result = std::move(result);
				handle.resume();
				}))
				return true;
			m_Result.Class = "error";
			m_Result.Results.Type = MiValue::Kind::Tuple;
			MiValue msg;
			msg.Text = "GDB is not running";
			m_Result.Results.Items.emplace_back("msg", std::move(msg));
			return false;
		}
		MiResult await_resume() {
			return std::move(m_Result);
		}

	private:
		GdbDebugger* m_Self;
		std::string m_Command;
		int m_Flags;
		MiResult m_Result;
	};

	// several commands sent at once (GDB runs them in order, without waiting for the caller in between)
	class GdbDebugger::BatchAwaiter {
	public:
		BatchAwaiter(GdbDebugger* self, std::vector<std::string> commands, int flags) : m_Self(self), m_Commands(std::move(commands)), m_Flags(flags) {}

		bool await_ready() const noexcept {
			return m_Commands.empty();
		}
		bool await_suspend(std::coroutine_handle<> handle) {
			m_Results.resize(m_Commands.size());
			m_Left = m_Commands.size();
			size_t sent = 0;
			for (size_t i = 0; i < m_Commands.size(); i++) {
				bool ok = m_Self->Send(std::move(m_Commands[i]), m_Flags, [this, handle, i](MiResult&& result) {
					m_Results[i] = std::move(result);
					if (--m_Left == 0)
						handle.resume();
				});
				if (!ok) {
					m_Results[i].Class = "error";
					m_Left--;
				}
				else
					sent++;
			}
			return sent > 0 && m_Left > 0;
		}
		std::vector<MiResult> await_resume() {
			return std::move(m_Results);
		}

	private:
		GdbDebugger* m_Self;
		std::vector<std::string> m_Commands;
		int m_Flags;
		std::vector<MiResult> m_Results;
		size_t m_Left{ 0 };
	};

	// the target stops (or the session ends) after the generation given
	class GdbDebugger::StopAwaiter {
	public:
		StopAwaiter(GdbDebugger* self, std::uint64_t generation) : m_Self(self), m_Generation(generation) {}

		bool await_ready() const noexcept {
			return m_Self->m_StopGeneration != m_Generation;
		}
		void await_suspend(std::coroutine_handle<> handle) {
			m_Self->m_StopWaiters.push_back(handle);
		}
		void await_resume() noexcept {}

	private:
		GdbDebugger* m_Self;
		std::uint64_t m_Generation;
	};

	// resumes the coroutine through the dispatcher, after what has been posted already: the notifications a call posts
	// are delivered before its task completes, as DbgEng's are
	class GdbDebugger::FlushAwaiter {
	public:
		explicit FlushAwaiter(GdbDebugger* self) : m_Self(self) {}
		bool await_ready() const noexcept {
			return !m_Self->m_Dispatcher;
		}
		void await_suspend(std::coroutine_handle<> handle) {
			if (!m_Self->m_Dispatcher->Post([handle] { handle.resume(); }))
				handle.resume();
		}
		void await_resume() noexcept {}

	private:
		GdbDebugger* m_Self;
	};

	GdbDebugger::FlushAwaiter GdbDebugger::Flush() {
		return FlushAwaiter(this);
	}

	GdbDebugger::BusyGuard::~BusyGuard() {
		if (--Self->m_Busy == 0)
			Self->Notify(DebuggerNotificationType::BusyChanged);
	}

	GdbDebugger::BusyGuard GdbDebugger::Busy() {
		if (m_Busy++ == 0)
			Notify(DebuggerNotificationType::BusyChanged);
		return BusyGuard(this);
	}

	//
	// Lifetime
	//

	GdbDebugger::GdbDebugger() = default;

	GdbDebugger::~GdbDebugger() {
		// (GDB kills what it started and detaches from what it attached to as it exits; the reader is joined, so nothing
		// is posted after this, and what was posted and has not run is dropped with the dispatcher)
		m_Gdb.Stop();
		m_Dispatcher.reset();
	}

	HRESULT GdbDebugger::Create(NotificationHandler handler, std::unique_ptr<IDispatcher> dispatcher) {
		if (!dispatcher)
			return E_INVALIDARG;		// (there is no window dispatcher to fall back on here)
		if (m_Gdb.IsRunning())
			return E_UNEXPECTED;
		m_Handler = std::move(handler);
		m_Dispatcher = std::move(dispatcher);

		GdbProcess::Callbacks callbacks;
		callbacks.Record = [this](MiRecord&& record) {
			m_Dispatcher->Post([this, record = std::move(record)]() mutable { OnRecord(record); });
		};
		callbacks.InferiorOutput = [this](std::string&& text) {
			auto wide = FromUtf8(text);
			m_Dispatcher->Post([this, wide = std::move(wide)]() mutable {
				Output(OutputDataMask::Debuggee, std::move(wide));
			});
		};
		callbacks.Exited = [this] {
			m_Dispatcher->Post([this] { OnGdbExited(); });
		};
		if (!m_Gdb.Start(std::move(callbacks), m_GdbPath))
			return E_FAIL;

		// (sent at once; GDB runs them before anything a caller sends later)
		const char* setup[] = {
			"-gdb-set mi-async on",
			"-gdb-set pagination off",
			"-gdb-set confirm off",
			"-gdb-set width 0",
			"-gdb-set height 0",
			"-gdb-set breakpoint pending on",
			"-gdb-set disassembly-flavor intel",
			"-gdb-set print object on",
			"-gdb-set print static-members off",
			// a debuginfod download can take minutes, and GDB would ask first; DebugX does not ask for it (yet)
			"-gdb-set debuginfod enabled off",
			"-enable-pretty-printing",
		};
		for (auto cmd : setup)
			Send(cmd, Capture, nullptr);
		Send("-inferior-tty-set " + MiQuote(m_Gdb.InferiorTty()), Capture, nullptr);
		Send("-interpreter-exec console \"info signals\"", Capture, [this](MiResult&& r) {
			m_DefaultFilters = ParseSignals(r.Console);
		});
		return S_OK;
	}

	//
	// Sending and receiving
	//

	bool GdbDebugger::Send(std::string command, int flags, std::function<void(MiResult&&)> done) {
		auto token = m_NextToken++;
		m_Pending.push_back({ token, flags, {}, {}, std::move(done) });
		if (!m_Gdb.Write(std::to_string(token) + command)) {
			m_Pending.pop_back();
			return false;
		}
		return true;
	}

	GdbDebugger::CommandAwaiter GdbDebugger::Command(std::string command, int flags) {
		return CommandAwaiter(this, std::move(command), flags);
	}

	GdbDebugger::BatchAwaiter GdbDebugger::Commands(std::vector<std::string> commands, int flags) {
		return BatchAwaiter(this, std::move(commands), flags);
	}

	GdbDebugger::StopAwaiter GdbDebugger::WaitForStop(std::uint64_t generation) {
		return StopAwaiter(this, generation);
	}

	void GdbDebugger::Post(std::function<void()> func) {
		if (m_Dispatcher)
			m_Dispatcher->Post(std::move(func));
	}

	void GdbDebugger::Notify(DebuggerNotificationType type) {
		Post([this, type] {
			if (m_Handler)
				m_Handler(type, nullptr);
		});
	}

	void GdbDebugger::Output(OutputDataMask mask, std::wstring text) {
		if (text.empty())
			return;
		OutputData data{ mask, std::move(text), false };
		Notify(DebuggerNotificationType::Output, std::move(data));
	}

	void GdbDebugger::SetTargetType(TargetType type) {
		// (in order with the notifications: whoever handles the ones before still sees the old type)
		Post([this, type] { m_TargetType = type; });
		Notify(DebuggerNotificationType::TargetChanged);
	}

	void GdbDebugger::OnRecord(MiRecord& record) {
		switch (record.Type) {
			case MiRecord::Kind::Result: {
				if (!record.Token)
					break;		// (Break's -exec-interrupt)
				auto it = std::find_if(m_Pending.begin(), m_Pending.end(), [&](auto& p) { return p.Token == *record.Token; });
				if (it == m_Pending.end())
					break;
				MiResult result{ record.Class, std::move(record.Results), std::move(it->Console), std::move(it->Log) };
				auto done = std::move(it->Done);
				m_Pending.erase(it);
				if (result.Class == "running" && m_Status != ExecutionStatus::Go) {
					// (*running normally comes first; this is in case it does not)
					m_Status = ExecutionStatus::Go;
					Notify(DebuggerNotificationType::StatusChanged, ExecutionStatus::Go);
				}
				// after what is already posted: the notifications that came before the result are delivered before the
				// caller goes on
				if (done)
					Post([done = std::move(done), result = std::move(result)]() mutable { done(std::move(result)); });
				break;
			}

			case MiRecord::Kind::Log: {
				// GDB 17 starts an error with an emoji (U+274C), a warning with another (U+26A0): left out, the error shown
				// as one. (The command's ^error repeats the message: ExecuteCommand does not show it again.)
				auto mask = OutputDataMask::Warning;
				std::string_view text = record.Text;
				auto strip = [&](std::string_view emoji) {
					if (!text.starts_with(emoji))
						return false;
					text.remove_prefix(emoji.size());
					if (text.starts_with("\xEF\xB8\x8F"))	// (the emoji presentation selector)
						text.remove_prefix(3);
					if (text.starts_with(' '))
						text.remove_prefix(1);
					return true;
				};
				if (strip("\xE2\x9D\x8C"))
					mask = OutputDataMask::Error;
				else
					strip("\xE2\x9A\xA0");
				if (!m_Pending.empty())
					m_Pending.front().Log += text;
				if (m_Pending.empty() || !(m_Pending.front().Flags & Capture))
					Output(mask, FromUtf8(std::string(text)));
				break;
			}

			case MiRecord::Kind::Console:
			case MiRecord::Kind::Target:
				if (!m_Pending.empty() && (m_Pending.front().Flags & Capture))
					m_Pending.front().Console += record.Text;
				else
					Output(record.Type == MiRecord::Kind::Console ? OutputDataMask::Normal : OutputDataMask::Debuggee, FromUtf8(record.Text));
				break;

			case MiRecord::Kind::Exec:
				if (record.Class == "running") {
					if (m_Status != ExecutionStatus::Go && m_HasTarget) {
						m_Status = ExecutionStatus::Go;
						Notify(DebuggerNotificationType::StatusChanged, ExecutionStatus::Go);
					}
				}
				else if (record.Class == "stopped")
					OnStopped(record.Results);
				break;

			case MiRecord::Kind::Notify:
				OnNotify(record);
				break;

			case MiRecord::Kind::Other:
				Output(OutputDataMask::Normal, FromUtf8(record.Text + "\n"));
				break;

			default:
				break;
		}
	}

	void GdbDebugger::OnStopped(const MiValue& results) {
		auto reason = results.Get("reason");
		if (reason.rfind("exited", 0) == 0) {
			if (!m_ExitPosted) {
				ExitThreadData data{};
				if (reason == "exited")
					data.ExitCode = static_cast<ULONG>(results.GetNumber("exit-code", 0, 8));
				else if (reason == "exited-signalled")
					data.ExitCode = 128 + SignalNumber(results.Get("signal-name"));
				Notify(DebuggerNotificationType::ExitProcess, data);
				m_ExitPosted = true;
			}
			if (m_LiveGroups.size() <= 1)
				EndSession();
			else
				StopsAdvanced();
			return;
		}
		if (!m_HasTarget) {
			StopsAdvanced();
			return;
		}

		if (results.Has("thread-id"))
			m_Thread = static_cast<ULONG>(results.GetNumber("thread-id", m_Thread, 10));
		m_Frame = 0;
		auto& frame = results["frame"];

		if (reason == "breakpoint-hit") {
			auto id = BreakpointNumber(results.Get("bkptno"));
			if (!m_Internal.contains(id))
				Notify(DebuggerNotificationType::Breakpoint, BreakpointData{ id });
		}
		else if (reason.find("watchpoint-trigger") != std::string::npos) {
			const MiValue* wpt = results.Find("wpt");
			if (!wpt) wpt = results.Find("hw-awpt");
			if (!wpt) wpt = results.Find("hw-rwpt");
			if (wpt)
				Notify(DebuggerNotificationType::Breakpoint, BreakpointData{ BreakpointNumber(wpt->Get("number")) });
		}
		else if (reason == "signal-received") {
			ExceptionData data{};
			data.Code = SignalNumber(results.Get("signal-name"));
			data.Address = frame.GetNumber("addr");
			data.FirstChance = true;
			auto lwp = m_Lwps.find(m_Thread);
			data.ThreadId = lwp != m_Lwps.end() ? lwp->second : 0;
			data.Name = FromUtf8(results.Get("signal-meaning"));
			Notify(DebuggerNotificationType::Exception, std::move(data));
		}

		// (who is current is known before anyone is told that the target has stopped)
		Notify(DebuggerNotificationType::ContextChanged, ContextData{ m_Thread });
		m_Status = ExecutionStatus::Break;
		Notify(DebuggerNotificationType::StatusChanged, ExecutionStatus::Break);
		StopsAdvanced();
	}

	void GdbDebugger::StopsAdvanced() {
		m_StopGeneration++;
		auto waiters = std::move(m_StopWaiters);
		m_StopWaiters.clear();
		for (auto h : waiters)
			Post([h] { h.resume(); });
	}

	void GdbDebugger::OnNotify(const MiRecord& record) {
		auto& r = record.Results;
		auto& cls = record.Class;
		if (cls == "thread-group-started") {
			m_LiveGroups.insert(r.Get("id"));
			m_Pid = static_cast<int>(r.GetNumber("pid", 0, 10));
			CreateProcessData data{};
			std::error_code ec;
			auto exe = fs::read_symlink(std::format("/proc/{}/exe", m_Pid), ec);
			data.ImageName = !ec ? exe.string() : m_ExePath;
			if (!m_HasTarget)
				StartSession(m_NextTargetType);
			Notify(DebuggerNotificationType::ProcessCreated, std::move(data));
		}
		else if (cls == "thread-group-exited") {
			m_LiveGroups.erase(r.Get("id"));
			if (r.Has("exit-code") && !m_ExitPosted) {
				Notify(DebuggerNotificationType::ExitProcess, ExitThreadData{ static_cast<ULONG>(r.GetNumber("exit-code", 0, 8)), 0, 0 });
				m_ExitPosted = true;
			}
			if (m_LiveGroups.empty())
				EndSession();
		}
		else if (cls == "thread-created") {
			ThreadCreateData data{};
			data.Id = static_cast<ULONG>(r.GetNumber("id", 0, 10));
			data.Pid = m_Pid;
			Notify(DebuggerNotificationType::ThreadCreated, data);
		}
		else if (cls == "thread-exited") {
			ExitThreadData data{};
			data.Id = static_cast<ULONG>(r.GetNumber("id", 0, 10));
			auto lwp = m_Lwps.find(data.Id);
			if (lwp != m_Lwps.end()) {
				data.ThreadId = lwp->second;
				m_Lwps.erase(lwp);
			}
			Notify(DebuggerNotificationType::ExitThread, data);
		}
		else if (cls == "thread-selected") {
			auto id = static_cast<ULONG>(r.GetNumber("id", 0, 10));
			auto frame = static_cast<ULONG>(r["frame"].GetNumber("level", 0, 10));
			if (id != m_Thread && id != 0) {
				m_Thread = id;
				m_Frame = 0;
				Notify(DebuggerNotificationType::ContextChanged, ContextData{ id });
			}
			if (frame != m_Frame) {
				m_Frame = frame;
				Notify(DebuggerNotificationType::ScopeChanged, ScopeData{ frame });
			}
		}
		else if (cls == "library-loaded" || cls == "library-unloaded") {
			ModuleEventData data{};
			auto& ranges = r["ranges"];
			if (!ranges.Items.empty())
				data.BaseAddress = ranges.Items[0].second.GetNumber("from");
			data.Name = FromUtf8(r.Get("target-name"));
			Notify(cls == "library-loaded" ? DebuggerNotificationType::ModuleLoaded : DebuggerNotificationType::ModuleUnloaded, std::move(data));
		}
		else if (cls == "breakpoint-created") {
			auto id = BreakpointNumber(r["bkpt"].Get("number"));
			// (GDB says so before the result of the command that created it, which is the oldest one waiting)
			if (!m_Pending.empty() && (m_Pending.front().Flags & Internal))
				m_Internal.insert(id);
			if (!m_Internal.contains(id))
				Notify(DebuggerNotificationType::BreakpointsChanged);
		}
		else if (cls == "breakpoint-modified") {
			if (!m_Internal.contains(BreakpointNumber(r["bkpt"].Get("number"))))
				Notify(DebuggerNotificationType::BreakpointsChanged);
		}
		else if (cls == "breakpoint-deleted") {
			auto id = BreakpointNumber(r.Get("id"));
			m_BreakpointSettings.erase(id);
			if (!m_Internal.contains(id))
				Notify(DebuggerNotificationType::BreakpointsChanged);
		}
		else if (cls == "memory-changed") {
			Notify(DebuggerNotificationType::MemoryChanged);
		}
	}

	void GdbDebugger::OnGdbExited() {
		auto pending = std::move(m_Pending);
		m_Pending.clear();
		for (auto& p : pending) {
			if (p.Done) {
				MiResult result;
				result.Class = "error";
				result.Results.Type = MiValue::Kind::Tuple;
				MiValue msg;
				msg.Text = "GDB has exited";
				result.Results.Items.emplace_back("msg", std::move(msg));
				Post([done = std::move(p.Done), result = std::move(result)]() mutable { done(std::move(result)); });
			}
		}
		Output(OutputDataMask::Error, L"GDB has exited\n");
		m_LiveGroups.clear();
		EndSession();
		StopsAdvanced();
	}

	void GdbDebugger::StartSession(TargetType type) {
		m_HasTarget = true;
		m_ExitPosted = false;
		m_Thread = 0;
		m_Frame = 0;
		m_Lwps.clear();
		m_RegisterNames.clear();
		m_GeneralRegisters.clear();
		SetTargetType(type);
	}

	void GdbDebugger::EndSession() {
		if (!m_HasTarget)
			return;
		m_HasTarget = false;
		m_Thread = 0;
		m_Frame = 0;
		m_Pid = 0;
		m_Status = ExecutionStatus::NoDebuggee;
		Notify(DebuggerNotificationType::StatusChanged, ExecutionStatus::NoDebuggee);
		SetTargetType(TargetType::Unknown);
		StopsAdvanced();
	}

	//
	// Helpers
	//

	HRESULT GdbDebugger::Fail(const MiResult& result, HRESULT hr) {
		auto msg = result.Message();
		m_LastError = FromUtf8(msg);
		if (Contains(msg, "not permitted") || Contains(msg, "permission denied"))
			return E_ACCESSDENIED;
		if (Contains(msg, "no such file"))
			return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
		return hr;
	}

	bool GdbDebugger::CanRun() const {
		return m_HasTarget && m_Status == ExecutionStatus::Break && (m_TargetType & TargetType::Live) == TargetType::Live;
	}

	bool GdbDebugger::IsLive() const {
		return m_HasTarget && (m_TargetType & TargetType::Live) == TargetType::Live;
	}

	std::string GdbDebugger::ThreadOption(bool withFrame) const {
		if (m_Thread == 0)
			return {};
		return withFrame ? std::format(" --thread {} --frame {}", m_Thread, m_Frame) : std::format(" --thread {}", m_Thread);
	}

	std::string GdbDebugger::Symbolize(const MiValue& frame) const {
		auto func = frame.Get("func");
		if (func.empty() || func == "??")
			return {};
		auto from = frame.Get("from");
		auto module = Basename(!from.empty() ? from : m_ExePath);
		return module.empty() ? func : module + "!" + func;
	}

	std::string GdbDebugger::QuoteLocation(std::wstring_view location) const {
		return MiQuote(ToUtf8(location));
	}

	Task<HRESULT> GdbDebugger::Interrupt() {
		if (m_Status == ExecutionStatus::Break || !m_HasTarget)
			co_return S_OK;
		auto gen = m_StopGeneration;
		auto r = co_await Command("-exec-interrupt --all");
		if (!r.Ok())
			co_return Fail(r);
		co_await WaitForStop(gen);
		co_return S_OK;
	}

	Task<HRESULT> GdbDebugger::RunAndWait(std::string command, int flags) {
		auto gen = m_StopGeneration;
		auto r = co_await Command(std::move(command), flags);
		if (!r.Ok())
			co_return Fail(r);
		if (r.Class == "running")
			co_await WaitForStop(gen);
		co_return S_OK;
	}

	//
	// Sessions
	//

	Task<HRESULT> GdbDebugger::LaunchAndAttach(PCWSTR exeName, PCWSTR commandLineArgs, PCWSTR workingDir, AttachType attach) {
		auto busy = Busy();
		if (m_HasTarget)
			co_return E_UNEXPECTED;
		std::string exe = ToUtf8(exeName ? exeName : L"");
		std::string args = ToUtf8(commandLineArgs ? commandLineArgs : L"");
		std::string cwd = ToUtf8(workingDir ? workingDir : L"");

		auto r = co_await Command("-file-exec-and-symbols " + MiQuote(exe));
		if (!r.Ok())
			co_return Fail(r);
		m_ExePath = exe;
		// (the rest of the line is the arguments, as a shell would split them)
		r = co_await Command("-exec-arguments " + args);
		if (!cwd.empty()) {
			r = co_await Command("-environment-cd " + MiQuote(cwd));
			if (!r.Ok())
				co_return Fail(r);
		}

		m_NextTargetType = TargetType::LiveUser;
		bool initialBreak = (attach & AttachType::InvasiveNoInitialBreak) != AttachType::InvasiveNoInitialBreak;
		auto gen = m_StopGeneration;
		// the initial break is at main (--start: a temporary breakpoint of GDB's, which is DebugX's own, hidden), else at the
		// first instruction (no main: a stripped or non-C program). Asked first: without a main, --start declines the
		// breakpoint and runs anyway, to the end.
		bool hasMain = false;
		if (initialBreak)
			hasMain = (co_await Command("-interpreter-exec console \"info address main\"", Capture | Internal)).Ok();
		if (!initialBreak)
			r = co_await Command("-exec-run", Capture | Internal);
		else if (hasMain)
			r = co_await Command("-exec-run --start", Capture | Internal);
		else
			r = co_await Command("-interpreter-exec console starti", Capture | Internal);
		if (!r.Ok())
			co_return Fail(r);
		if (initialBreak)
			co_await WaitForStop(gen);
		co_return m_HasTarget ? S_OK : E_FAIL;
	}

	Task<HRESULT> GdbDebugger::AttachToProcess(DWORD pid, AttachType attach) {
		auto busy = Busy();
		if (m_HasTarget)
			co_return E_UNEXPECTED;
		std::error_code ec;
		auto exe = fs::read_symlink(std::format("/proc/{}/exe", pid), ec);
		m_ExePath = ec ? std::string() : exe.string();
		m_NextTargetType = TargetType::LiveUser;
		auto gen = m_StopGeneration;
		auto r = co_await Command(std::format("-target-attach {}", pid));
		if (!r.Ok())
			co_return Fail(r);
		co_await WaitForStop(gen);
		if (!m_HasTarget)
			co_return E_FAIL;
		if ((attach & (AttachType::InvasiveNoInitialBreak | AttachType::InvasiveResumeProcess)) != AttachType::Default) {
			r = co_await Command("-exec-continue");
			if (!r.Ok())
				co_return Fail(r);
		}
		co_return S_OK;
	}

	Task<HRESULT> GdbDebugger::OpenDumpFile(PCWSTR filename) {
		auto busy = Busy();
		if (m_HasTarget)
			co_return E_UNEXPECTED;
		m_NextTargetType = TargetType::DumpUser;
		auto r = co_await Command("-target-select core " + MiQuote(ToUtf8(filename)));
		if (!r.Ok())
			co_return Fail(r);
		if (!m_HasTarget)
			StartSession(TargetType::DumpUser);
		// the program the core is of, as GDB found it
		auto files = co_await Command("-interpreter-exec console \"info inferiors\"");
		if (m_ExePath.empty()) {
			std::istringstream lines(files.Console);
			std::string line;
			while (std::getline(lines, line)) {
				auto slash = line.find('/');
				if (line.find('*') != std::string::npos && slash != std::string::npos) {
					auto path = line.substr(slash);
					while (!path.empty() && std::isspace(static_cast<unsigned char>(path.back())))
						path.pop_back();
					m_ExePath = path;
				}
			}
		}
		auto info = co_await Command("-thread-info");
		m_Thread = static_cast<ULONG>(info.Results.GetNumber("current-thread-id", 1, 10));
		m_Status = ExecutionStatus::Break;
		Notify(DebuggerNotificationType::ContextChanged, ContextData{ m_Thread });
		Notify(DebuggerNotificationType::StatusChanged, ExecutionStatus::Break);
		co_await Flush();
		co_return S_OK;
	}

	Task<HRESULT> GdbDebugger::EndTarget(bool terminate) {
		if (!m_HasTarget)
			co_return S_OK;
		if ((m_TargetType & TargetType::Dump) == TargetType::Dump) {
			co_await Command("-interpreter-exec console core-file");
			m_LiveGroups.clear();
			EndSession();
			co_return S_OK;
		}
		auto hr = co_await Interrupt();
		if (FAILED(hr))
			co_return hr;
		if (!m_HasTarget)
			co_return S_OK;
		auto gen = m_StopGeneration;
		auto r = co_await Command(terminate ? "-interpreter-exec console kill" : "-target-detach");
		if (!r.Ok())
			co_return Fail(r);
		if (m_HasTarget)
			co_await WaitForStop(gen);
		co_return S_OK;
	}

	Task<HRESULT> GdbDebugger::Stop() {
		auto busy = Busy();
		co_return co_await EndTarget(true);
	}

	Task<HRESULT> GdbDebugger::Detach() {
		auto busy = Busy();
		co_return co_await EndTarget(false);
	}

	Task<HRESULT> GdbDebugger::EndCurrentProcess(bool terminate) {
		auto busy = Busy();
		co_return co_await EndTarget(terminate);
	}

	Task<HRESULT> GdbDebugger::ExecuteCommand(PCWSTR command) {
		auto busy = Busy();
		auto gen = m_StopGeneration;
		auto r = co_await Command("-interpreter-exec console " + MiQuote(ToUtf8(command)), 0);
		if (!r.Ok()) {
			// (GDB has usually logged the error already, in order with the rest of the output)
			if (r.Log.find(r.Message()) == std::string::npos)
				Output(OutputDataMask::Error, FromUtf8(r.Message() + "\n"));
			co_return Fail(r);
		}
		// (a run waits for the stop, as a run of the GUI's does)
		if (r.Class == "running")
			co_await WaitForStop(gen);
		co_return S_OK;
	}

	Task<HRESULT> GdbDebugger::OutputPrompt() {
		Output(OutputDataMask::Prompt, L"(gdb) ");
		co_return S_OK;
	}

	//
	// Enumerations
	//

	Task<HRESULT> GdbDebugger::EnumModules(std::vector<ModuleInfo>& modules) {
		auto busy = Busy();
		modules.clear();
		if (!m_HasTarget)
			co_return E_UNEXPECTED;
		auto r = co_await Command("-file-list-shared-libraries");
		std::map<std::string, bool> symbols;
		for (auto& [_, lib] : r.Results["shared-libraries"].Items)
			symbols[lib.Get("target-name")] = lib.Get("symbols-loaded") == "1";

		if (IsLive() && m_Pid) {
			// from the process's mappings: a module from its lowest mapping to its highest
			std::map<std::string, std::pair<ULONG64, ULONG64>> ranges;
			std::vector<std::string> order;
			for (auto& region : ReadMaps(m_Pid)) {
				if (region.Path.empty() || region.Path[0] != '/')
					continue;
				auto it = ranges.find(region.Path);
				if (it == ranges.end()) {
					ranges[region.Path] = { region.Start, region.End };
					order.push_back(region.Path);
				}
				else {
					it->second.first = std::min(it->second.first, region.Start);
					it->second.second = std::max(it->second.second, region.End);
				}
			}
			for (auto& path : order) {
				auto [start, end] = ranges[path];
				ModuleInfo m{};
				m.BaseAddress = start;
				m.Size = static_cast<ULONG>(end - start);
				auto s = symbols.find(path);
				// (DbgEng's numbers, which the Modules pane names: 3 loaded, 5 deferred)
				m.Symbols = path == m_ExePath || (s != symbols.end() && s->second) ? 3 : 5;
				m.Name = FromUtf8(Basename(path));
				m.ImageName = FromUtf8(path);
				modules.push_back(std::move(m));
			}
		}
		else {
			if (!m_ExePath.empty()) {
				ModuleInfo m{};
				m.Symbols = 3;
				m.Name = FromUtf8(Basename(m_ExePath));
				m.ImageName = FromUtf8(m_ExePath);
				modules.push_back(std::move(m));
			}
			for (auto& [_, lib] : r.Results["shared-libraries"].Items) {
				ModuleInfo m{};
				auto& ranges = lib["ranges"];
				if (!ranges.Items.empty()) {
					m.BaseAddress = ranges.Items[0].second.GetNumber("from");
					m.Size = static_cast<ULONG>(ranges.Items[0].second.GetNumber("to") - m.BaseAddress);
				}
				m.Symbols = lib.Get("symbols-loaded") == "1" ? 3 : 5;
				m.Name = FromUtf8(Basename(lib.Get("target-name")));
				m.ImageName = FromUtf8(lib.Get("target-name"));
				modules.push_back(std::move(m));
			}
		}
		co_return S_OK;
	}

	Task<HRESULT> GdbDebugger::EnumProcesses(std::vector<ProcessInfo>& processes, std::vector<ThreadInfo>& threads) {
		auto busy = Busy();
		processes.clear();
		threads.clear();
		if (!m_HasTarget)
			co_return E_UNEXPECTED;
		auto results = co_await Commands({ "-list-thread-groups --recurse 1", "-thread-info" });
		auto current = static_cast<ULONG>(results[1].Results.GetNumber("current-thread-id", m_Thread, 10));

		long ticks = ::sysconf(_SC_CLK_TCK);
		double uptime = 0;
		{
			std::ifstream f("/proc/uptime");
			f >> uptime;
		}

		for (auto& [_, group] : results[0].Results["groups"].Items) {
			if (!group.Has("pid"))
				continue;
			ProcessInfo p{};
			p.Id = static_cast<ULONG>(ParseNumber(group.Get("id").substr(1), 0, 10));
			p.ProcessId = static_cast<ULONG>(group.GetNumber("pid", 0, 10));
			p.ImagePath = FromUtf8(group.Get("executable"));
			if (IsLive()) {
				std::ifstream stat(std::format("/proc/{}/stat", p.ProcessId));
				std::string text((std::istreambuf_iterator<char>(stat)), {});
				auto close = text.rfind(')');
				if (close != std::string::npos) {
					std::istringstream rest(text.substr(close + 2));
					std::string field;
					unsigned long long start = 0;
					for (int i = 3; i <= 22 && rest >> field; i++)
						if (i == 22)
							start = std::strtoull(field.c_str(), nullptr, 10);
					if (ticks > 0 && start > 0)
						p.UpTimeSec = static_cast<ULONG>(std::max(0.0, uptime - static_cast<double>(start) / ticks));
				}
			}
			for (auto& [__, t] : group["threads"].Items) {
				ThreadInfo info{};
				info.Id = static_cast<ULONG>(t.GetNumber("id", 0, 10));
				auto target = t.Get("target-id");
				for (auto prefix : { "LWP ", "process ", "Thread " }) {
					auto pos = target.find(prefix);
					if (pos != std::string::npos) {
						info.ThreadId = static_cast<ULONG>(ParseNumber(target.substr(pos + strlen(prefix)), 0, 10));
						if (std::string_view(prefix) != "Thread ")
							break;
					}
				}
				m_Lwps[info.Id] = info.ThreadId;
				info.Pid = p.ProcessId;
				info.ProcessId = p.Id;
				info.Name = FromUtf8(t.Get("name"));
				auto& frame = t["frame"];
				info.InstructionOffset = frame.GetNumber("addr");
				info.Location = FromUtf8(Symbolize(frame));
				info.Current = info.Id == current;
				if (info.Current)
					p.Current = true;
				threads.push_back(std::move(info));
				p.ThreadCount++;
			}
			processes.push_back(std::move(p));
		}
		if (processes.size() == 1)
			processes[0].Current = true;
		co_return S_OK;
	}

	Task<HRESULT> GdbDebugger::EnumHandles(std::vector<HandleInfo>& handles) {
		auto busy = Busy();
		handles.clear();
		if (!IsLive() || !m_Pid)
			co_return E_NOTIMPL;
		// a process's file descriptors (/proc/<pid>/fd): the Linux counterpart of its handles
		std::error_code ec;
		for (auto& entry : fs::directory_iterator(std::format("/proc/{}/fd", m_Pid), ec)) {
			HandleInfo h{};
			h.Handle = ParseNumber(entry.path().filename().string(), 0, 10);
			auto target = fs::read_symlink(entry.path(), ec).string();
			std::string type = "File";
			if (target.rfind("socket:", 0) == 0) type = "Socket";
			else if (target.rfind("pipe:", 0) == 0) type = "Pipe";
			else if (target.rfind("anon_inode:", 0) == 0) type = target.substr(11);
			else if (target.rfind("/dev/", 0) == 0) type = "Device";
			h.Type = FromUtf8(type);
			h.Name = FromUtf8(target);
			handles.push_back(std::move(h));
		}
		std::sort(handles.begin(), handles.end(), [](auto& a, auto& b) { return a.Handle < b.Handle; });
		co_return ec && handles.empty() ? E_ACCESSDENIED : S_OK;
	}

	Task<HRESULT> GdbDebugger::SearchMemory(ULONG64 start, std::vector<BYTE> pattern, ULONG64& found, ULONG64 kernelRange) {
		auto busy = Busy();
		if (pattern.empty())
			co_return E_INVALIDARG;
		if (!IsLive() || !m_Pid)
			co_return E_NOTIMPL;
		std::string bytes;
		for (auto b : pattern)
			bytes += std::format(", 0x{:02x}", b);
		for (auto& region : ReadMaps(m_Pid)) {
			if (!region.Readable || region.End <= start || region.Path == "[vvar]" || region.Path == "[vsyscall]")
				continue;
			auto from = std::max(region.Start, start);
			if (region.End - from < pattern.size())
				continue;
			auto r = co_await Command(std::format("-interpreter-exec console \"find /b 0x{:x}, 0x{:x}{}\"", from, region.End - 1, bytes));
			if (!r.Ok())
				continue;
			auto pos = r.Console.find("0x");
			if (pos != std::string::npos && r.Console.find("not found") == std::string::npos) {
				found = ParseNumber(r.Console.substr(pos), 0, 16);
				co_return S_OK;
			}
		}
		co_return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
	}

	Task<HRESULT> GdbDebugger::EnumServerProcesses(std::vector<ServerProcess>& processes) {
		processes.clear();
		std::error_code ec;
		for (auto& entry : fs::directory_iterator("/proc", ec)) {
			auto name = entry.path().filename().string();
			if (name.empty() || !std::all_of(name.begin(), name.end(), ::isdigit))
				continue;
			std::ifstream comm(entry.path() / "comm");
			std::string command;
			std::getline(comm, command);
			processes.push_back({ static_cast<ULONG>(std::stoul(name)), FromUtf8(command) });
		}
		co_return S_OK;
	}

	Task<HRESULT> GdbDebugger::Script(ScriptAction action, PCWSTR path, std::wstring& output) {
		auto busy = Busy();
		// GDB's scripts (its command files and Python) are sourced; they cannot be unloaded
		if (action == ScriptAction::Unload)
			co_return E_NOTIMPL;
		auto r = co_await Command("-interpreter-exec console " + MiQuote("source " + ToUtf8(path)));
		output = FromUtf8(r.Console);
		if (!r.Ok())
			output += FromUtf8(r.Message() + "\n");
		Output(OutputDataMask::Normal, output);
		co_return r.Ok() ? S_OK : Fail(r);
	}

	//
	// Threads, frames, registers
	//

	Task<HRESULT> GdbDebugger::GetRegisters(std::vector<RegisterInfo>& registers) {
		auto busy = Busy();
		registers.clear();
		if (!m_HasTarget)
			co_return E_UNEXPECTED;
		if (m_RegisterNames.empty()) {
			// the names, and which are the general ones ("info registers" shows the general group)
			auto results = co_await Commands({ "-data-list-register-names", "-interpreter-exec console \"info registers\"" });
			for (auto& [_, name] : results[0].Results["register-names"].Items)
				m_RegisterNames.push_back(name.Text);
			std::istringstream lines(results[1].Console);
			std::string line;
			while (std::getline(lines, line)) {
				std::istringstream words(line);
				std::string name;
				words >> name;
				auto it = std::find(m_RegisterNames.begin(), m_RegisterNames.end(), name);
				if (it != m_RegisterNames.end())
					m_GeneralRegisters.push_back(static_cast<int>(it - m_RegisterNames.begin()));
			}
		}
		std::string numbers;
		for (auto n : m_GeneralRegisters)
			numbers += std::format(" {}", n);
		auto r = co_await Command("-data-list-register-values" + ThreadOption() + " --skip-unavailable x" + numbers);
		if (!r.Ok())
			co_return Fail(r);
		for (auto& [_, v] : r.Results["register-values"].Items) {
			auto n = v.GetNumber("number", ~0ull, 10);
			if (n >= m_RegisterNames.size() || m_RegisterNames[n].empty())
				continue;
			auto value = v.Get("value");
			if (!value.empty() && value[0] == '{')
				continue;		// (a vector register)
			registers.push_back({ FromUtf8(m_RegisterNames[n]), FromUtf8(value) });
		}
		co_return S_OK;
	}

	Task<HRESULT> GdbDebugger::GetStackTrace(std::vector<StackFrameInfo>& frames, ULONG maxFrames) {
		auto busy = Busy();
		frames.clear();
		if (!m_HasTarget || m_Thread == 0)
			co_return E_UNEXPECTED;
		auto r = co_await Command(std::format("-stack-list-frames{} 0 {}", ThreadOption(false), maxFrames ? maxFrames - 1 : 0));
		if (!r.Ok())
			co_return Fail(r);
		std::vector<std::string> regs;
		for (auto& [_, f] : r.Results["stack"].Items) {
			StackFrameInfo info{};
			info.Index = static_cast<ULONG>(f.GetNumber("level", 0, 10));
			info.InstructionOffset = f.GetNumber("addr");
			info.Symbol = FromUtf8(Symbolize(f));
			frames.push_back(std::move(info));
			regs.push_back(std::format("-data-evaluate-expression --thread {} --frame {} \"(unsigned long long)$sp\"", m_Thread, frames.back().Index));
			regs.push_back(std::format("-data-evaluate-expression --thread {} --frame {} \"(unsigned long long)$fp\"", m_Thread, frames.back().Index));
		}
		for (size_t i = 0; i + 1 < frames.size(); i++)
			frames[i].ReturnOffset = frames[i + 1].InstructionOffset;
		auto values = co_await Commands(std::move(regs));
		for (size_t i = 0; i < frames.size() && 2 * i + 1 < values.size(); i++) {
			frames[i].StackOffset = values[2 * i].Results.GetNumber("value");
			frames[i].FrameOffset = values[2 * i + 1].Results.GetNumber("value");
		}
		co_return S_OK;
	}

	Task<HRESULT> GdbDebugger::SetCurrentThread(ULONG threadId) {
		auto busy = Busy();
		if (!m_HasTarget)
			co_return E_UNEXPECTED;
		auto r = co_await Command(std::format("-thread-select {}", threadId));
		if (!r.Ok())
			co_return Fail(r, E_INVALIDARG);
		m_Thread = threadId;
		m_Frame = 0;
		Notify(DebuggerNotificationType::ContextChanged, ContextData{ threadId });
		co_await Flush();
		co_return S_OK;
	}

	Task<HRESULT> GdbDebugger::SetCurrentProcess(ULONG processId) {
		auto busy = Busy();
		if (!m_HasTarget)
			co_return E_UNEXPECTED;
		auto r = co_await Command(std::format("-interpreter-exec console \"inferior {}\"", processId));
		if (!r.Ok())
			co_return Fail(r, E_INVALIDARG);
		auto info = co_await Command("-thread-info");
		m_Thread = static_cast<ULONG>(info.Results.GetNumber("current-thread-id", m_Thread, 10));
		m_Frame = 0;
		auto groups = co_await Command("-list-thread-groups");
		for (auto& [_, g] : groups.Results["groups"].Items)
			if (g.Get("id") == std::format("i{}", processId))
				m_Pid = static_cast<int>(g.GetNumber("pid", m_Pid, 10));
		Notify(DebuggerNotificationType::ContextChanged, ContextData{ m_Thread });
		co_await Flush();
		co_return S_OK;
	}

	Task<HRESULT> GdbDebugger::SetDebugChildProcesses(bool debug) {
		auto busy = Busy();
		// both sides of a fork stay with GDB (as inferiors of their own); off: the child is let go
		auto results = co_await Commands({ std::string("-gdb-set detach-on-fork ") + (debug ? "off" : "on"), std::string("-gdb-set schedule-multiple ") + (debug ? "on" : "off") });
		co_return results[0].Ok() ? S_OK : Fail(results[0]);
	}

	Task<HRESULT> GdbDebugger::SetScopeFrame(ULONG frameIndex) {
		auto busy = Busy();
		if (!m_HasTarget || m_Thread == 0)
			co_return E_UNEXPECTED;
		// (GDB's selected frame too, so that commands typed in the Command view are about it, as in WinDbg after .frame)
		auto r = co_await Command(std::format("-stack-select-frame{} {}", ThreadOption(false), frameIndex));
		if (!r.Ok())
			co_return Fail(r, E_INVALIDARG);
		m_Frame = frameIndex;
		Notify(DebuggerNotificationType::ScopeChanged, ScopeData{ frameIndex });
		co_await Flush();
		co_return S_OK;
	}

	Task<HRESULT> GdbDebugger::GetScope(ULONG64& instructionOffset, ULONG& frameIndex) {
		auto busy = Busy();
		if (!m_HasTarget || m_Thread == 0)
			co_return E_UNEXPECTED;
		auto r = co_await Command("-stack-info-frame" + ThreadOption());
		if (!r.Ok())
			co_return Fail(r);
		instructionOffset = r.Results["frame"].GetNumber("addr");
		frameIndex = m_Frame;
		co_return S_OK;
	}

	//
	// Values: variable objects (GDB's equivalent of the data model), with the pretty-printers (libstdc++'s for the
	// standard library) behind them
	//

	Task<std::vector<MiValue>> GdbDebugger::ListChildren(std::string var) {
		std::vector<MiValue> result;
		auto r = co_await Command(std::format("-var-list-children --all-values {} 0 {}", var, MaxElements));
		for (auto& [_, child] : r.Results["children"].Items) {
			auto exp = child.Get("exp");
			// a C++ class's members come under "public", "private" and "protected": flattened
			if (!child.Has("type") && (exp == "public" || exp == "private" || exp == "protected")) {
				auto inner = co_await ListChildren(child.Get("name"));
				for (auto& c : inner)
					result.push_back(std::move(c));
			}
			else
				result.push_back(child);
		}
		co_return result;
	}

	namespace {
		// GDB's path expressions parenthesize every step, ((shape).Origin).Y: the parentheses around a plain name or member
		// chain are dropped where a member or element access follows (not before an operand: (int)x is a cast)
		std::string SimplifyPath(std::string expr) {
			for (bool changed = true; changed; ) {
				changed = false;
				for (size_t open = expr.find('('); open != std::string::npos; open = expr.find('(', open + 1)) {
					auto close = expr.find(')', open);
					if (close == std::string::npos)
						break;
					auto inner = std::string_view(expr).substr(open + 1, close - open - 1);
					if (inner.empty() || !(std::isalpha(static_cast<unsigned char>(inner[0])) || inner[0] == '_') ||
						!std::all_of(inner.begin(), inner.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.'; }))
						continue;
					char next = close + 1 < expr.size() ? expr[close + 1] : '\0';
					if (next != '.' && next != '[' && next != '-' && next != ')' && next != '\0')
						continue;
					expr.erase(close, 1);
					expr.erase(open, 1);
					changed = true;
					break;
				}
			}
			return expr;
		}

		// GDB's value of a structure is "{...}" and of an array "[3]": they get a summary of their members, as the data
		// model's are on Windows ({X=1 Y=2}, {7, 8, 9})
		bool NeedsSummary(const std::wstring& value) {
			if (value == L"{...}")
				return true;
			return value.size() > 2 && value.front() == L'[' && value.back() == L']' &&
				std::all_of(value.begin() + 1, value.end() - 1, [](wchar_t c) { return c >= L'0' && c <= L'9'; });
		}

		std::wstring Summary(const std::vector<MiValue>& children, bool array) {
			std::wstring summary = L"{";
			for (auto& c : children) {
				if (summary.size() > 1)
					summary += array ? L", " : L" ";
				auto value = FromUtf8(c.Has("value") ? c.Get("value") : "{...}");
				summary += array ? value : FromUtf8(c.Get("exp")) + L"=" + value;
				if (summary.size() > 200) {
					summary += L" ...";
					break;
				}
			}
			return summary + L"}";
		}

		bool IsExpandable(const MiValue& v) {
			return v.GetNumber("numchild", 0, 10) > 0 || (v.Get("dynamic") == "1" && v.Get("has_more") == "1") ||
				(v.Get("dynamic") == "1" && v.Get("displayhint") != "string" && v.GetNumber("numchild", 0, 10) > 0);
		}
	}

	Task<void> GdbDebugger::AddChildren(std::vector<SymbolEntry>& entries, std::vector<std::pair<size_t, std::string>>& needPaths, std::vector<MiValue> children,
		std::wstring parentPath, ULONG depth, const std::set<std::wstring>& expanded) {
		for (auto& child : children) {
			SymbolEntry e;
			e.Name = FromUtf8(child.Get("exp"));
			e.Path = parentPath + L"\n" + e.Name;
			e.Type = FromUtf8(child.Get("type"));
			e.Value = FromUtf8(child.Get("value"));
			e.Depth = depth;
			e.Expandable = IsExpandable(child);
			auto var = child.Get("name");
			std::vector<MiValue> grandChildren;
			if (e.Expandable && (expanded.contains(e.Path) || NeedsSummary(e.Value)))
				grandChildren = co_await ListChildren(var);
			if (NeedsSummary(e.Value) && !grandChildren.empty())
				e.Value = Summary(grandChildren, e.Value[0] == L'[');
			if (!e.Expandable && child.Get("dynamic") != "1")
				needPaths.emplace_back(entries.size(), var);
			e.Expanded = e.Expandable && expanded.contains(e.Path);
			auto path = e.Path;
			bool recurse = e.Expanded;
			entries.push_back(std::move(e));
			if (recurse)
				co_await AddChildren(entries, needPaths, std::move(grandChildren), path, depth + 1, expanded);
		}
	}

	Task<HRESULT> GdbDebugger::BuildTree(std::vector<SymbolEntry>& entries, std::vector<std::pair<std::wstring, std::string>> roots, std::set<std::wstring> expanded) {
		for (auto& [name, expr] : roots) {
			auto r = co_await Command("-var-create" + ThreadOption() + " - * " + MiQuote(expr));
			SymbolEntry e;
			e.Name = name;
			e.Path = name;
			if (!r.Ok()) {
				e.Value = FromUtf8(r.Message());
				e.Error = true;
				entries.push_back(std::move(e));
				continue;
			}
			auto var = r.Results.Get("name");
			e.Type = FromUtf8(r.Results.Get("type"));
			e.Value = FromUtf8(r.Results.Get("value"));
			e.Expandable = IsExpandable(r.Results);
			std::vector<std::pair<size_t, std::string>> needPaths;
			std::vector<MiValue> children;
			if (e.Expandable && (expanded.contains(e.Path) || NeedsSummary(e.Value)))
				children = co_await ListChildren(var);
			if (NeedsSummary(e.Value) && !children.empty())
				e.Value = Summary(children, e.Value[0] == L'[');
			if (!e.Expandable && r.Results.Get("dynamic") != "1")
				e.Expression = FromUtf8(expr);
			e.Expanded = e.Expandable && expanded.contains(e.Path);
			bool recurse = e.Expanded;
			auto path = e.Path;
			entries.push_back(std::move(e));
			if (recurse)
				co_await AddChildren(entries, needPaths, std::move(children), path, 1, expanded);

			// the C++ expressions of the members that are values (what AssignValue takes)
			if (!needPaths.empty()) {
				std::vector<std::string> commands;
				for (auto& [_, v] : needPaths)
					commands.push_back("-var-info-path-expression " + v);
				auto paths = co_await Commands(std::move(commands));
				for (size_t i = 0; i < paths.size(); i++)
					if (paths[i].Ok())
						entries[needPaths[i].first].Expression = FromUtf8(SimplifyPath(paths[i].Results.Get("path_expr")));
			}
			Send("-var-delete " + var, Capture, nullptr);
		}
		co_return S_OK;
	}

	Task<HRESULT> GdbDebugger::GetLocals(std::vector<SymbolEntry>& entries, std::vector<std::wstring> expanded) {
		auto busy = Busy();
		entries.clear();
		if (!m_HasTarget || m_Thread == 0)
			co_return E_UNEXPECTED;
		auto r = co_await Command("-stack-list-variables" + ThreadOption() + " --no-values");
		if (!r.Ok())
			co_return Fail(r);
		std::vector<std::pair<std::wstring, std::string>> roots;
		std::set<std::string> seen;
		for (auto& [_, v] : r.Results["variables"].Items) {
			auto name = v.Get("name");
			if (seen.insert(name).second)		// (a shadowed name: the innermost)
				roots.emplace_back(FromUtf8(name), name);
		}
		co_return co_await BuildTree(entries, std::move(roots), std::set<std::wstring>(expanded.begin(), expanded.end()));
	}

	Task<HRESULT> GdbDebugger::EvaluateWatches(std::vector<SymbolEntry>& entries, std::vector<std::wstring> expressions, std::vector<std::wstring> expanded) {
		auto busy = Busy();
		entries.clear();
		std::vector<std::pair<std::wstring, std::string>> roots;
		for (auto& e : expressions)
			roots.emplace_back(e, ToUtf8(e));
		if (!m_HasTarget) {
			for (auto& [name, _] : roots)
				entries.push_back({ name, name, {}, L"No target", {}, 0, false, false, true });
			co_return S_OK;
		}
		co_return co_await BuildTree(entries, std::move(roots), std::set<std::wstring>(expanded.begin(), expanded.end()));
	}

	HRESULT GdbDebugger::SetDisplayRadix(ULONG radix) {
		if (radix != 10 && radix != 16)
			return E_INVALIDARG;
		m_Radix = radix;
		return Send(std::format("-gdb-set output-radix {}", radix), Capture, nullptr) ? S_OK : E_FAIL;
	}

	Task<HRESULT> GdbDebugger::AssignValue(PCWSTR expression, PCWSTR value, std::wstring& error) {
		auto busy = Busy();
		error.clear();
		if (!m_HasTarget)
			co_return E_UNEXPECTED;
		auto r = co_await Command("-data-evaluate-expression" + ThreadOption() + " " + MiQuote("(" + ToUtf8(expression) + ") = (" + ToUtf8(value) + ")"));
		if (!r.Ok()) {
			error = FromUtf8(r.Message());
			co_return Fail(r);
		}
		Notify(DebuggerNotificationType::MemoryChanged);
		co_await Flush();
		co_return S_OK;
	}

	//
	// Breakpoints
	//

	Task<HRESULT> GdbDebugger::ListBreakpoints(std::vector<BreakpointInfo>& breakpoints) {
		breakpoints.clear();
		auto r = co_await Command("-break-list");
		if (!r.Ok())
			co_return Fail(r);
		for (auto& [_, bp] : r.Results["BreakpointTable"]["body"].Items) {
			auto id = BreakpointNumber(bp.Get("number"));
			auto type = bp.Get("type");
			if (m_Internal.contains(id) || (type.find("breakpoint") == std::string::npos && type.find("watchpoint") == std::string::npos))
				continue;
			BreakpointInfo info{};
			auto known = m_BreakpointSettings.find(id);
			if (known != m_BreakpointSettings.end())
				static_cast<BreakpointSettings&>(info) = known->second;
			info.Id = id;
			info.Enabled = bp.Get("enabled") == "y";
			info.OneShot = bp.Get("disp") == "del";
			info.Condition = FromUtf8(bp.Get("cond"));
			info.Hits = static_cast<ULONG>(bp.GetNumber("times", 0, 10));
			info.ThreadId = bp.Has("thread") ? static_cast<ULONG>(bp.GetNumber("thread", AnyId, 10)) : AnyId;
			info.IsData = type.find("watchpoint") != std::string::npos;
			if (known == m_BreakpointSettings.end()) {
				info.PassCount = static_cast<ULONG>(bp.GetNumber("ignore", 0, 10)) + 1;
				if (info.IsData) {
					info.Expression = FromUtf8(bp.Get("what"));
					info.DataAccess = type.find("acc") != std::string::npos || type.find("read") != std::string::npos ? BreakOnRead : BreakOnWrite;
				}
				else
					info.Expression = FromUtf8(bp.Get("original-location"));
				std::wstring commands;
				for (auto& [__, line] : bp["script"].Items)
					commands += (commands.empty() ? L"" : L"\n") + FromUtf8(line.Text);
				info.Command = commands;
			}
			auto addr = bp.Get("addr");
			info.Deferred = addr == "<PENDING>";
			if (addr == "<MULTIPLE>" && !bp["locations"].Items.empty())
				info.Offset = bp["locations"].Items[0].second.GetNumber("addr");
			else
				info.Offset = ParseNumber(addr);
			auto original = bp.Get("original-location");
			info.Location = !original.empty() ? FromUtf8(original) : info.Expression;
			breakpoints.push_back(std::move(info));
		}
		co_return S_OK;
	}

	Task<HRESULT> GdbDebugger::EnumBreakpoints(std::vector<BreakpointInfo>& breakpoints) {
		auto busy = Busy();
		co_return co_await ListBreakpoints(breakpoints);
	}

	Task<HRESULT> GdbDebugger::InsertBreakpoint(const BreakpointSettings& settings) {
		std::string expr = ToUtf8(settings.Expression);
		if (expr.empty())
			co_return E_INVALIDARG;
		MiResult r;
		ULONG id = 0;
		if (!settings.IsData) {
			std::string cmd = "-break-insert -f";
			if (settings.OneShot) cmd += " -t";
			if (!settings.Enabled) cmd += " -d";
			if (!settings.Condition.empty()) cmd += " -c " + MiQuote(ToUtf8(settings.Condition));
			if (settings.PassCount > 1) cmd += std::format(" -i {}", settings.PassCount - 1);
			if (settings.ThreadId != AnyId) cmd += std::format(" -p {}", settings.ThreadId);
			r = co_await Command(cmd + " " + MiQuote(expr));
			if (!r.Ok())
				co_return Fail(r, E_INVALIDARG);
			id = BreakpointNumber(r.Results["bkpt"].Get("number"));
		}
		else {
			// the address is worked out once, as DbgEng's data breakpoints are
			ULONG64 address;
			auto ev = co_await Command("-data-evaluate-expression" + ThreadOption() + " " + MiQuote("(unsigned long long)(" + expr + ")"));
			if (!ev.Ok())
				co_return Fail(ev, E_INVALIDARG);
			address = ev.Results.GetNumber("value");
			const char* type = settings.DataSize == 1 ? "unsigned char" : settings.DataSize == 2 ? "unsigned short" :
				settings.DataSize == 4 ? "unsigned int" : settings.DataSize == 8 ? "unsigned long long" : nullptr;
			if (!type || address % settings.DataSize)
				co_return E_INVALIDARG;
			if (settings.DataAccess & BreakOnExecute)
				r = co_await Command(std::format("-break-insert -h {}\"*0x{:x}\"", settings.OneShot ? "-t " : "", address));
			else
				r = co_await Command(std::format("-break-watch {}\"*({}*)0x{:x}\"", (settings.DataAccess & BreakOnRead) ? "-a " : "", type, address));
			if (!r.Ok())
				co_return Fail(r, E_INVALIDARG);
			for (auto key : { "bkpt", "wpt", "hw-awpt", "hw-rwpt" })
				if (r.Results.Has(key))
					id = BreakpointNumber(r.Results[key].Get("number"));
			std::vector<std::string> more;
			if (!settings.Condition.empty())
				more.push_back(std::format("-break-condition {} ", id) + ToUtf8(settings.Condition));
			if (settings.PassCount > 1)
				more.push_back(std::format("-break-after {} {}", id, settings.PassCount - 1));
			if (!settings.Enabled)
				more.push_back(std::format("-break-disable {}", id));
			co_await Commands(std::move(more));
		}
		if (!settings.Command.empty()) {
			std::string cmd = std::format("-break-commands {}", id);
			std::istringstream lines(ToUtf8(settings.Command));
			std::string line;
			while (std::getline(lines, line))
				if (!line.empty())
					cmd += " " + MiQuote(line);
			co_await Command(cmd);
		}
		m_BreakpointSettings[id] = settings;
		Notify(DebuggerNotificationType::BreakpointsChanged);
		co_await Flush();
		co_return S_OK;
	}

	Task<HRESULT> GdbDebugger::AddBreakpoint(BreakpointSettings settings) {
		auto busy = Busy();
		co_return co_await InsertBreakpoint(settings);
	}

	Task<HRESULT> GdbDebugger::UpdateBreakpoint(ULONG id, BreakpointSettings settings) {
		auto busy = Busy();
		std::vector<BreakpointInfo> list;
		co_await ListBreakpoints(list);
		auto it = std::find_if(list.begin(), list.end(), [&](auto& b) { return b.Id == id; });
		if (it == list.end() || it->IsData != settings.IsData)
			co_return E_INVALIDARG;
		auto& old = *it;
		if (old.Expression != settings.Expression || old.ThreadId != settings.ThreadId || old.OneShot != settings.OneShot ||
			(settings.IsData && (old.DataSize != settings.DataSize || old.DataAccess != settings.DataAccess))) {
			// (made again: GDB cannot move a breakpoint; it gets a new number)
			auto r = co_await Command(std::format("-break-delete {}", id));
			if (!r.Ok())
				co_return Fail(r);
			co_return co_await InsertBreakpoint(settings);
		}
		std::vector<std::string> commands;
		if (old.Condition != settings.Condition)
			commands.push_back(settings.Condition.empty() ? std::format("-interpreter-exec console \"condition {}\"", id) :
				std::format("-break-condition {} ", id) + ToUtf8(settings.Condition));
		if (old.PassCount != settings.PassCount)
			commands.push_back(std::format("-break-after {} {}", id, settings.PassCount > 0 ? settings.PassCount - 1 : 0));
		if (old.Enabled != settings.Enabled)
			commands.push_back(std::format("{} {}", settings.Enabled ? "-break-enable" : "-break-disable", id));
		if (old.Command != settings.Command) {
			std::string cmd = std::format("-break-commands {}", id);
			std::istringstream lines(ToUtf8(settings.Command));
			std::string line;
			while (std::getline(lines, line))
				if (!line.empty())
					cmd += " " + MiQuote(line);
			commands.push_back(cmd);
		}
		auto results = co_await Commands(std::move(commands));
		for (auto& r : results)
			if (!r.Ok())
				co_return Fail(r, E_INVALIDARG);
		m_BreakpointSettings[id] = settings;
		Notify(DebuggerNotificationType::BreakpointsChanged);
		co_await Flush();
		co_return S_OK;
	}

	Task<HRESULT> GdbDebugger::RemoveBreakpoint(ULONG id) {
		auto busy = Busy();
		auto r = co_await Command(std::format("-break-delete {}", id));
		if (!r.Ok())
			co_return Fail(r, E_INVALIDARG);
		m_BreakpointSettings.erase(id);
		Notify(DebuggerNotificationType::BreakpointsChanged);
		co_await Flush();
		co_return S_OK;
	}

	Task<HRESULT> GdbDebugger::SetBreakpointEnabled(ULONG id, bool enabled) {
		auto busy = Busy();
		auto r = co_await Command(std::format("{} {}", enabled ? "-break-enable" : "-break-disable", id));
		if (!r.Ok())
			co_return Fail(r, E_INVALIDARG);
		auto known = m_BreakpointSettings.find(id);
		if (known != m_BreakpointSettings.end())
			known->second.Enabled = enabled;
		Notify(DebuggerNotificationType::BreakpointsChanged);
		co_await Flush();
		co_return S_OK;
	}

	std::wstring GdbDebugger::AddressLocation(ULONG64 address) const {
		return std::format(L"*0x{:x}", address);
	}

	std::wstring GdbDebugger::SourceLineLocation(std::wstring_view file, ULONG line) const {
		// a linespec; a file name with spaces or colons is quoted
		if (file.find_first_of(L" :'") != std::wstring_view::npos)
			return std::format(L"\"{}\":{}", file, line);
		return std::format(L"{}:{}", file, line);
	}

	std::wstring GdbDebugger::StackPointerExpression() const {
		return L"$sp";
	}

	Task<HRESULT> GdbDebugger::ResolveLocation(PCWSTR location, ULONG64& address) {
		auto busy = Busy();
		std::wstring loc = location ? location : L"";
		if (!loc.empty() && loc[0] == L'*') {
			auto r = co_await Command("-data-evaluate-expression " + MiQuote("(unsigned long long)(" + ToUtf8(loc.substr(1)) + ")"));
			if (!r.Ok())
				co_return Fail(r, E_INVALIDARG);
			address = r.Results.GetNumber("value");
			co_return S_OK;
		}
		// (a disabled temporary breakpoint of DebugX's own: GDB resolves a location the way it would for a breakpoint)
		auto r = co_await Command("-break-insert -t -d " + QuoteLocation(loc), Capture | Internal);
		if (!r.Ok())
			co_return Fail(r, E_INVALIDARG);
		auto& bp = r.Results["bkpt"];
		auto addr = bp.Get("addr");
		if (addr == "<MULTIPLE>" && !bp["locations"].Items.empty())
			address = bp["locations"].Items[0].second.GetNumber("addr");
		else
			address = ParseNumber(addr);
		Send("-break-delete " + bp.Get("number"), Capture, nullptr);
		co_return address ? S_OK : E_INVALIDARG;
	}

	//
	// Running
	//

	Task<HRESULT> GdbDebugger::Go() {
		auto busy = Busy();
		if (!CanRun())
			co_return E_UNEXPECTED;
		co_return co_await RunAndWait("-exec-continue");
	}

	Task<HRESULT> GdbDebugger::RunTo(PCWSTR location) {
		auto busy = Busy();
		if (!CanRun())
			co_return E_UNEXPECTED;
		// a temporary breakpoint of DebugX's own, in the current thread only (as DbgEng's g <address>)
		auto r = co_await Command(std::format("-break-insert -t{} {}", m_Thread ? std::format(" -p {}", m_Thread) : "", QuoteLocation(location)), Capture | Internal);
		if (!r.Ok())
			co_return Fail(r, E_INVALIDARG);
		auto number = r.Results["bkpt"].Get("number");
		auto hr = co_await RunAndWait("-exec-continue");
		// (if something else stopped it first; an error if it was hit, and GDB removed it)
		if (m_HasTarget)
			Send("-break-delete " + number, Capture, nullptr);
		co_return hr;
	}

	Task<HRESULT> GdbDebugger::GetInstructionOffset(ULONG64& offset) {
		auto busy = Busy();
		if (!m_HasTarget || m_Thread == 0)
			co_return E_UNEXPECTED;
		auto r = co_await Command(std::format("-data-evaluate-expression --thread {} --frame 0 \"(unsigned long long)$pc\"", m_Thread));
		if (!r.Ok())
			co_return Fail(r);
		offset = r.Results.GetNumber("value");
		co_return S_OK;
	}

	Task<HRESULT> GdbDebugger::Disassemble(ULONG64 address, ULONG count, std::vector<DisassemblyLine>& lines) {
		auto busy = Busy();
		lines.clear();
		if (!m_HasTarget)
			co_return E_UNEXPECTED;
		// (at most 15 bytes an instruction on x86)
		auto r = co_await Command(std::format("-data-disassemble -s 0x{:x} -e 0x{:x} -- 2", address, address + static_cast<ULONG64>(count) * 15));
		if (!r.Ok())
			co_return Fail(r, HRESULT_FROM_WIN32(ERROR_PARTIAL_COPY));
		for (auto& [_, insn] : r.Results["asm_insns"].Items) {
			if (lines.size() >= count)
				break;
			DisassemblyLine line;
			line.Address = insn.GetNumber("address");
			auto opcodes = insn.Get("opcodes");
			auto func = insn.Get("func-name");
			std::string where = func.empty() ? std::string() : std::format("<{}+{}>", func, insn.Get("offset"));
			line.Text = FromUtf8(std::format("{:016x} {:<24} {:<30} {}", line.Address, opcodes, insn.Get("inst"), where));
			while (!line.Text.empty() && line.Text.back() == L' ')
				line.Text.pop_back();
			lines.push_back(std::move(line));
		}
		co_return lines.empty() ? HRESULT_FROM_WIN32(ERROR_PARTIAL_COPY) : S_OK;
	}

	Task<HRESULT> GdbDebugger::GetSourceLocations(std::vector<ULONG64> offsets, std::vector<SourceLocation>& locations) {
		auto busy = Busy();
		locations.assign(offsets.size(), {});
		// (the symbols are enough: GDB knows the lines of the program before it runs, and after)
		std::vector<std::string> commands;
		for (auto offset : offsets)
			commands.push_back(std::format("-interpreter-exec console \"info line *0x{:x}\"", offset));
		auto results = co_await Commands(std::move(commands));
		std::vector<size_t> which;
		for (size_t i = 0; i < results.size(); i++) {
			// Line 9 of "loop.cpp" starts at address 0x1149 <Work(int)+12> and ends at 0x1150 <Work(int)+19>.
			auto& text = results[i].Console;
			if (text.rfind("Line ", 0) != 0)
				continue;
			auto of = text.find(" of \"");
			auto close = of == std::string::npos ? std::string::npos : text.find('"', of + 5);
			if (close == std::string::npos)
				continue;
			locations[i].Line = static_cast<ULONG>(ParseNumber(text.substr(5), 0, 10));
			locations[i].File = FromUtf8(text.substr(of + 5, close - of - 5));
			which.push_back(i);
		}
		// where the files are on this machine (GDB's source path, else where they were compiled)
		std::map<std::wstring, std::wstring> found;
		for (auto i : which)
			found[locations[i].File];
		if (found.empty())
			co_return S_OK;
		// (the files' full names come from the list of them)
		auto files = co_await Command("-file-list-exec-source-files");
		for (auto& [_, f] : files.Results["files"].Items) {
			auto file = FromUtf8(f.Get("file"));
			auto full = FromUtf8(f.Get("fullname"));
			if (found.contains(file) && !full.empty() && found[file].empty() && fs::exists(ToUtf8(full)))
				found[file] = full;
		}
		for (auto i : which) {
			auto& loc = locations[i];
			loc.Path = found[loc.File];
			if (loc.Path.empty() && !loc.File.empty() && loc.File[0] == L'/' && fs::exists(ToUtf8(loc.File)))
				loc.Path = loc.File;
		}
		co_return S_OK;
	}

	Task<std::string> GdbDebugger::FunctionAt(ULONG64 address) {
		auto r = co_await Command(std::format("-data-disassemble -s 0x{:x} -e 0x{:x} -- 0", address, address + 1));
		auto& insns = r.Results["asm_insns"].Items;
		co_return insns.empty() ? std::string() : insns[0].second.Get("func-name");
	}

	Task<HRESULT> GdbDebugger::Step(StepKind kind, bool bySourceLine) {
		auto busy = Busy();
		if (!CanRun())
			co_return E_UNEXPECTED;
		bool byLine = false;
		if (bySourceLine && kind != StepKind::Out) {
			// by a source line only where the thread has line information: GDB's next/step there would run on until it found some
			auto r = co_await Command(std::format("-stack-info-frame --thread {} --frame 0", m_Thread));
			byLine = r.Ok() && r.Results["frame"].Has("line");
		}
		std::string cmd;
		switch (kind) {
			case StepKind::Into: cmd = byLine ? "-exec-step" : "-exec-step-instruction"; break;
			case StepKind::Over: cmd = byLine ? "-exec-next" : "-exec-next-instruction"; break;
			case StepKind::Out: cmd = std::format("-exec-finish --frame {}", m_Frame); break;
		}
		co_return co_await RunAndWait(cmd + std::format(" --thread {}", m_Thread));
	}

	Task<HRESULT> GdbDebugger::SetInstructionOffset(ULONG64 offset, bool otherFunction) {
		auto busy = Busy();
		if (!CanRun())
			co_return E_UNEXPECTED;
		if (!otherFunction) {
			auto pc = co_await Command(std::format("-data-evaluate-expression --thread {} --frame 0 \"(unsigned long long)$pc\"", m_Thread));
			auto here = co_await FunctionAt(pc.Results.GetNumber("value"));
			auto there = co_await FunctionAt(offset);
			if (here != there)
				co_return SetNextOtherFunction;
		}
		auto r = co_await Command(std::format("-data-evaluate-expression --thread {} --frame 0 \"$pc = 0x{:x}\"", m_Thread, offset));
		if (!r.Ok())
			co_return Fail(r);
		m_Frame = 0;
		Notify(DebuggerNotificationType::ContextChanged, ContextData{ m_Thread });
		co_await Flush();
		co_return S_OK;
	}

	//
	// Paths, symbols, dumps
	//

	Task<HRESULT> GdbDebugger::GetSymbolPaths(std::wstring& symbolPath, std::wstring& sourcePath) {
		auto busy = Busy();
		auto results = co_await Commands({ "-gdb-show debug-file-directory", "-environment-directory" });
		symbolPath = FromUtf8(results[0].Results.Get("value"));
		sourcePath = FromUtf8(results[1].Results.Get("source-path"));
		co_return S_OK;
	}

	Task<HRESULT> GdbDebugger::SetSymbolPath(PCWSTR path) {
		auto busy = Busy();
		// the folders of separate debug files (GDB's debug-file-directory, ':'-separated)
		std::string value;
		for (auto& p : SplitPaths(path ? path : L""))
			value += (value.empty() ? "" : ":") + p;
		auto r = co_await Command("-gdb-set debug-file-directory " + value);
		co_return r.Ok() ? S_OK : Fail(r);
	}

	Task<HRESULT> GdbDebugger::SetSourcePath(PCWSTR path) {
		auto busy = Busy();
		std::string cmd = "-environment-directory -r";
		for (auto& p : SplitPaths(path ? path : L""))
			if (p != "$cdir" && p != "$cwd")
				cmd += " " + MiQuote(p);
		auto r = co_await Command(cmd);
		co_return r.Ok() ? S_OK : Fail(r);
	}

	Task<HRESULT> GdbDebugger::ReloadSymbols(PCWSTR module) {
		auto busy = Busy();
		if (!m_HasTarget)
			co_return E_UNEXPECTED;
		std::string cmd = "sharedlibrary";
		if (module && *module)
			cmd += " " + ToUtf8(module);
		auto r = co_await Command("-interpreter-exec console " + MiQuote(cmd));
		if (m_Status == ExecutionStatus::Break)
			Notify(DebuggerNotificationType::ContextChanged, ContextData{ m_Thread });
		co_await Flush();
		co_return r.Ok() ? S_OK : Fail(r);
	}

	Task<HRESULT> GdbDebugger::WriteDump(PCWSTR path, bool full) {
		auto busy = Busy();
		if (!IsLive())
			co_return E_NOTIMPL;
		if (m_Status != ExecutionStatus::Break)
			co_return E_UNEXPECTED;
		// a core file (always all of the memory GDB can read: gcore has no smaller kind)
		auto r = co_await Command("-interpreter-exec console " + MiQuote("gcore " + ToUtf8(path)));
		co_return r.Ok() ? S_OK : Fail(r);
	}

	//
	// Memory, expressions, registers
	//

	Task<HRESULT> GdbDebugger::Evaluate(PCWSTR expression, ULONG64& value) {
		auto busy = Busy();
		auto r = co_await Command("-data-evaluate-expression" + ThreadOption() + " " + MiQuote("(unsigned long long)(" + ToUtf8(expression) + ")"));
		if (!r.Ok())
			co_return Fail(r, E_INVALIDARG);
		auto text = r.Results.Get("value");
		if (text.empty() || !(std::isdigit(static_cast<unsigned char>(text[0])) || text[0] == '-'))
			co_return E_INVALIDARG;
		value = ParseNumber(text);
		co_return S_OK;
	}

	Task<HRESULT> GdbDebugger::ReadMemory(ULONG64 address, std::vector<BYTE>& buffer, ULONG size) {
		auto busy = Busy();
		buffer.clear();
		if (!m_HasTarget)
			co_return E_UNEXPECTED;
		if (size == 0)
			co_return S_OK;
		auto r = co_await Command(std::format("-data-read-memory-bytes 0x{:x} {}", address, size));
		if (!r.Ok())
			co_return HRESULT_FROM_WIN32(ERROR_PARTIAL_COPY);
		// the readable blocks, in order; what is read is what follows the address without a gap
		auto expected = address;
		for (auto& [_, block] : r.Results["memory"].Items) {
			if (block.GetNumber("begin") != expected)
				break;
			auto contents = block.Get("contents");
			for (size_t i = 0; i + 1 < contents.size(); i += 2)
				buffer.push_back(static_cast<BYTE>(std::stoi(contents.substr(i, 2), nullptr, 16)));
			expected = block.GetNumber("end");
		}
		co_return buffer.empty() ? HRESULT_FROM_WIN32(ERROR_PARTIAL_COPY) : S_OK;
	}

	Task<HRESULT> GdbDebugger::WriteMemory(ULONG64 address, std::vector<BYTE> data) {
		auto busy = Busy();
		if (!IsLive())
			co_return E_UNEXPECTED;
		if (data.empty())
			co_return S_OK;
		std::string hex;
		for (auto b : data)
			hex += std::format("{:02x}", b);
		auto r = co_await Command(std::format("-data-write-memory-bytes 0x{:x} {}", address, hex));
		if (!r.Ok())
			co_return Fail(r, HRESULT_FROM_WIN32(ERROR_PARTIAL_COPY));
		Notify(DebuggerNotificationType::MemoryChanged);
		co_await Flush();
		co_return S_OK;
	}

	Task<HRESULT> GdbDebugger::SetRegister(PCWSTR name, PCWSTR value) {
		auto busy = Busy();
		if (!IsLive() || m_Thread == 0)
			co_return E_UNEXPECTED;
		std::string reg = ToUtf8(name);
		if (reg.find("mm") != std::string::npos && (reg[0] == 'x' || reg[0] == 'y' || reg[0] == 'z'))
			co_return E_NOTIMPL;
		std::string text = ToUtf8(value);
		// hexadecimal by default for an integer register, as GetRegisters shows them; st0..st7 take a number as it is
		bool isFloat = reg.rfind("st", 0) == 0 && reg.size() == 3;
		if (!isFloat && !text.empty() && text.rfind("0x", 0) != 0 && std::all_of(text.begin(), text.end(), ::isxdigit))
			text = "0x" + text;
		auto r = co_await Command(std::format("-data-evaluate-expression --thread {} --frame {} ", m_Thread, m_Frame) + MiQuote("$" + reg + " = " + text));
		if (!r.Ok())
			co_return Fail(r, E_INVALIDARG);
		Notify(DebuggerNotificationType::ContextChanged, ContextData{ m_Thread });
		co_await Flush();
		co_return S_OK;
	}

	//
	// Event filters: GDB's signal handling (handle SIGx stop/print/pass)
	//

	std::vector<EventFilter> GdbDebugger::ParseSignals(const std::string& text) const {
		// Signal        Stop	Print	Pass to program	Description
		// SIGHUP        Yes	Yes	Yes		Hangup
		std::vector<EventFilter> filters;
		std::istringstream lines(text);
		std::string line;
		while (std::getline(lines, line)) {
			std::istringstream words(line);
			std::string name, stop, print, pass;
			if (!(words >> name >> stop >> print >> pass) || name.rfind("SIG", 0) != 0)
				continue;
			int number = SignalNumber(name);
			if (number == 0)
				continue;
			std::string description;
			std::getline(words, description);
			description.erase(0, description.find_first_not_of(" \t"));
			EventFilter f;
			f.Name = FromUtf8(name + (description.empty() ? "" : " (" + description + ")"));
			f.Code = number;
			f.IsException = true;
			f.ExecutionOption = stop == "Yes" ? FilterBreak : print == "Yes" ? FilterOutput : FilterIgnore;
			f.ContinueOption = pass == "Yes" ? FilterGoNotHandled : FilterGoHandled;
			filters.push_back(std::move(f));
		}
		return filters;
	}

	Task<HRESULT> GdbDebugger::GetEventFilters(std::vector<EventFilter>& filters, bool defaults) {
		auto busy = Busy();
		if (defaults && !m_DefaultFilters.empty()) {
			filters = m_DefaultFilters;
			co_return S_OK;
		}
		auto r = co_await Command("-interpreter-exec console \"info signals\"");
		if (!r.Ok())
			co_return Fail(r);
		filters = ParseSignals(r.Console);
		if (m_DefaultFilters.empty())
			m_DefaultFilters = filters;
		co_return S_OK;
	}

	Task<HRESULT> GdbDebugger::SetEventFilters(std::vector<EventFilter> filters) {
		auto busy = Busy();
		std::vector<std::string> commands;
		for (auto& f : filters) {
			if (!f.IsException || f.Code == 0)
				continue;
			const char* action = f.ExecutionOption == FilterBreak || f.ExecutionOption == FilterSecondChanceBreak ? "stop print" :
				f.ExecutionOption == FilterOutput ? "nostop print" : "nostop noprint";
			commands.push_back(std::format("-interpreter-exec console \"handle {} {} {}\"", SignalName(f.Code), action,
				f.ContinueOption == FilterGoNotHandled ? "pass" : "nopass"));
		}
		auto results = co_await Commands(std::move(commands));
		for (auto& r : results)
			if (!r.Ok())
				co_return Fail(r, E_INVALIDARG);
		co_return S_OK;
	}

	HRESULT GdbDebugger::Break() {
		// (any thread: a line to GDB, no token -- its result is not waited for; the stop comes as *stopped)
		return m_Gdb.Write("-exec-interrupt --all") ? S_OK : E_FAIL;
	}

	TargetType GdbDebugger::GetTargetType() const {
		return m_TargetType;
	}

	bool GdbDebugger::IsBusy() const {
		return m_Busy > 0;
	}
}
