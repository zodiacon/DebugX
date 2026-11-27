#pragma once

#include <coroutine>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>
#include "IDebugger.h"
#include "GdbProcess.h"
#include "MiParser.h"

//
// IDebugger over GDB/MI: GDB runs as a child process (GdbProcess) and every call is one or more MI commands, each with a
// token its result record comes back with. Nothing here blocks: a command is written to GDB's stdin and the coroutine
// suspends; the reader thread hands each record to the dispatcher, and on the dispatcher's thread (the caller's, the UI
// thread) the record completes the command, or becomes notifications. All state is on that thread: no locks, except in
// GdbProcess's writes (Break writes from any thread).
//
// GDB runs in all-stop, async mode (mi-async on): a run command answers ^running at once and *stopped comes when the
// target stops; Go, Step and RunTo complete at the stop. GDB attributes the console output of a command to it (it comes
// before its result record, and GDB runs commands in order), so the output of the commands DebugX sends for itself is
// captured, and only what the user asked for (ExecuteCommand) or the program and GDB say on their own become Output
// notifications.
//
// Ids: a thread is GDB's global thread number (ThreadInfo::Id; its LWP is ThreadInfo::ThreadId), a process an inferior's
// number (i1: 1), a breakpoint GDB's breakpoint number. The breakpoints DebugX sets for itself (RunTo, ResolveLocation,
// the stop at main) are left out of EnumBreakpoints and BreakpointsChanged.
//
namespace GdbEngine {
	class GdbDebugger : public DebugEngine::IDebugger {
	public:
		GdbDebugger();
		~GdbDebugger() override;

		// the GDB to run (before Create); empty: $DEBUGX_GDB, else "gdb" from PATH
		void SetGdbPath(std::string path) {
			m_GdbPath = std::move(path);
		}
		// GDB's message for the last call that failed (as the GUI's status bar may show it)
		const std::wstring& LastError() const {
			return m_LastError;
		}
		// Whether GDB downloads the debug information of what has none (the program, its libraries) from the debuginfod
		// servers -- what a symbol server is on Windows. On by default; the servers are $DEBUGINFOD_URLS, else the
		// system's (/etc/debuginfod/*.urls: the variable is set only in login shells). Takes effect for what is loaded
		// afterwards; a download is shown as output ("Downloading ... separate debug info for ...").
		void SetDownloadSymbols(bool download);
		bool DownloadSymbols() const {
			return m_DownloadSymbols;
		}
		// the debuginfod servers GDB uses (empty: none, nothing is downloaded)
		static std::string DebuginfodUrls();

		HRESULT Create(NotificationHandler handler, std::unique_ptr<DebugEngine::IDispatcher> dispatcher = nullptr) override;

		DebugEngine::Task<HRESULT> OpenDumpFile(PCWSTR filename) override;
		DebugEngine::Task<HRESULT> AttachToProcess(DWORD pid, DebugEngine::AttachType attach = DebugEngine::AttachType::Default) override;
		DebugEngine::Task<HRESULT> LaunchAndAttach(PCWSTR exeName, PCWSTR commandLineArgs = nullptr, PCWSTR workingDir = nullptr, DebugEngine::AttachType attach = DebugEngine::AttachType::Default) override;
		DebugEngine::Task<HRESULT> ExecuteCommand(PCWSTR command) override;
		DebugEngine::Task<HRESULT> OutputPrompt() override;
		DebugEngine::Task<HRESULT> Stop() override;
		DebugEngine::Task<HRESULT> Detach() override;
		DebugEngine::Task<HRESULT> EndCurrentProcess(bool terminate) override;

		DebugEngine::Task<HRESULT> EnumModules(std::vector<DebugEngine::ModuleInfo>& modules) override;
		DebugEngine::Task<HRESULT> EnumProcesses(std::vector<DebugEngine::ProcessInfo>& processes, std::vector<DebugEngine::ThreadInfo>& threads) override;
		DebugEngine::Task<HRESULT> EnumHandles(std::vector<DebugEngine::HandleInfo>& handles) override;
		DebugEngine::Task<HRESULT> SearchMemory(ULONG64 start, std::vector<BYTE> pattern, ULONG64& found, ULONG64 kernelRange = 0x10000000) override;
		DebugEngine::Task<HRESULT> EnumServerProcesses(std::vector<DebugEngine::ServerProcess>& processes) override;
		DebugEngine::Task<HRESULT> Script(ScriptAction action, PCWSTR path, std::wstring& output) override;

		DebugEngine::Task<HRESULT> GetRegisters(std::vector<DebugEngine::RegisterInfo>& registers) override;
		DebugEngine::Task<HRESULT> GetStackTrace(std::vector<DebugEngine::StackFrameInfo>& frames, ULONG maxFrames = 128) override;
		DebugEngine::Task<HRESULT> SetCurrentThread(ULONG threadId) override;
		DebugEngine::Task<HRESULT> SetCurrentProcess(ULONG processId) override;
		DebugEngine::Task<HRESULT> SetDebugChildProcesses(bool debug) override;

		DebugEngine::Task<HRESULT> SetScopeFrame(ULONG frameIndex) override;
		DebugEngine::Task<HRESULT> GetScope(ULONG64& instructionOffset, ULONG& frameIndex) override;
		DebugEngine::Task<HRESULT> GetLocals(std::vector<DebugEngine::SymbolEntry>& entries, std::vector<std::wstring> expanded) override;
		DebugEngine::Task<HRESULT> EvaluateWatches(std::vector<DebugEngine::SymbolEntry>& entries, std::vector<std::wstring> expressions, std::vector<std::wstring> expanded) override;
		HRESULT SetDisplayRadix(ULONG radix) override;
		DebugEngine::Task<HRESULT> AssignValue(PCWSTR expression, PCWSTR value, std::wstring& error) override;

		DebugEngine::Task<HRESULT> EnumBreakpoints(std::vector<DebugEngine::BreakpointInfo>& breakpoints) override;
		using IDebugger::AddBreakpoint;
		DebugEngine::Task<HRESULT> AddBreakpoint(DebugEngine::BreakpointSettings settings) override;
		DebugEngine::Task<HRESULT> UpdateBreakpoint(ULONG id, DebugEngine::BreakpointSettings settings) override;
		DebugEngine::Task<HRESULT> RemoveBreakpoint(ULONG id) override;
		DebugEngine::Task<HRESULT> SetBreakpointEnabled(ULONG id, bool enabled) override;

		std::wstring AddressLocation(ULONG64 address) const override;
		std::wstring SourceLineLocation(std::wstring_view file, ULONG line) const override;
		DebugEngine::Task<HRESULT> ResolveLocation(PCWSTR location, ULONG64& address) override;
		std::wstring StackPointerExpression() const override;

		DebugEngine::Task<HRESULT> Go() override;
		DebugEngine::Task<HRESULT> RunTo(PCWSTR location) override;
		DebugEngine::Task<HRESULT> GetInstructionOffset(ULONG64& offset) override;
		DebugEngine::Task<HRESULT> Disassemble(ULONG64 address, ULONG count, std::vector<DebugEngine::DisassemblyLine>& lines) override;
		DebugEngine::Task<HRESULT> GetSourceLocations(std::vector<ULONG64> offsets, std::vector<DebugEngine::SourceLocation>& locations) override;
		DebugEngine::Task<HRESULT> Step(DebugEngine::StepKind kind, bool bySourceLine) override;
		DebugEngine::Task<HRESULT> SetInstructionOffset(ULONG64 offset, bool otherFunction = false) override;

		DebugEngine::Task<HRESULT> GetSymbolPaths(std::wstring& symbolPath, std::wstring& sourcePath) override;
		DebugEngine::Task<HRESULT> SetSymbolPath(PCWSTR path) override;
		DebugEngine::Task<HRESULT> SetSourcePath(PCWSTR path) override;
		DebugEngine::Task<HRESULT> ReloadSymbols(PCWSTR module = nullptr) override;
		DebugEngine::Task<HRESULT> WriteDump(PCWSTR path, bool full) override;

		DebugEngine::Task<HRESULT> Evaluate(PCWSTR expression, ULONG64& value) override;
		DebugEngine::Task<HRESULT> ReadMemory(ULONG64 address, std::vector<BYTE>& buffer, ULONG size) override;
		DebugEngine::Task<HRESULT> WriteMemory(ULONG64 address, std::vector<BYTE> data) override;
		DebugEngine::Task<HRESULT> SetRegister(PCWSTR name, PCWSTR value) override;

		DebugEngine::Task<HRESULT> GetEventFilters(std::vector<DebugEngine::EventFilter>& filters, bool defaults = false) override;
		DebugEngine::Task<HRESULT> SetEventFilters(std::vector<DebugEngine::EventFilter> filters) override;

		HRESULT Break() override;
		DebugEngine::TargetType GetTargetType() const override;
		bool IsBusy() const override;

		// the most children a variable shows (an array's elements, a container's)
		static constexpr int MaxElements = 200;

	private:
		// what a command came back with
		struct MiResult {
			std::string Class;		// done, running, connected, error, exit
			MiValue Results;
			std::string Console;	// its console output, if captured
			std::string Log;		// what GDB logged while it ran (errors, warnings), whether it was captured or not
			bool Ok() const {
				return Class != "error";
			}
			std::string Message() const {
				return Results.Get("msg");
			}
		};

		enum CommandFlags {
			Capture = 1,		// the command's console output goes to MiResult::Console, not to Output notifications
			Internal = 2,		// the breakpoints it creates are DebugX's own
		};

		struct PendingCommand {
			std::uint64_t Token;
			int Flags;
			std::string Console;
			std::string Log;
			std::function<void(MiResult&&)> Done;
		};

		class CommandAwaiter;
		class BatchAwaiter;
		class StopAwaiter;
		class FlushAwaiter;
		struct BusyGuard {
			GdbDebugger* Self;
			BusyGuard(GdbDebugger* self) : Self(self) {}
			BusyGuard(const BusyGuard&) = delete;
			~BusyGuard();
		};
		struct VarNode;

		// sending
		bool Send(std::string command, int flags, std::function<void(MiResult&&)> done);
		CommandAwaiter Command(std::string command, int flags = Capture);
		BatchAwaiter Commands(std::vector<std::string> commands, int flags = Capture);
		StopAwaiter WaitForStop(std::uint64_t generation);
		FlushAwaiter Flush();
		BusyGuard Busy();

		// receiving (on the dispatcher's thread)
		void OnRecord(MiRecord& record);
		void OnStopped(const MiValue& results);
		void OnNotify(const MiRecord& record);
		void OnGdbExited();
		void StartSession(DebugEngine::TargetType type);
		void EndSession();
		void StopsAdvanced();

		// notifications, in order, through the dispatcher
		void Post(std::function<void()> func);
		void Notify(DebugEngine::DebuggerNotificationType type);
		template<typename T>
		void Notify(DebugEngine::DebuggerNotificationType type, T data) {
			auto p = std::make_shared<T>(std::move(data));
			Post([this, type, p] {
				if (m_Handler)
					m_Handler(type, p.get());
			});
		}
		void Output(DebugEngine::OutputDataMask mask, std::wstring text);
		void SetTargetType(DebugEngine::TargetType type);

		// helpers
		HRESULT Fail(const MiResult& result, HRESULT hr = E_FAIL);
		bool CanRun() const;
		bool IsLive() const;
		std::string ThreadOption(bool withFrame = true) const;
		// module!function of a frame record
		std::string Symbolize(const MiValue& frame) const;
		// the file mapped at an address (/proc/<pid>/maps, read once a stop); empty if none
		std::string MappedFile(ULONG64 address) const;
		std::string QuoteLocation(std::wstring_view location) const;
		DebugEngine::Task<HRESULT> Interrupt();
		DebugEngine::Task<HRESULT> EndTarget(bool terminate);
		DebugEngine::Task<HRESULT> ListBreakpoints(std::vector<DebugEngine::BreakpointInfo>& breakpoints);
		DebugEngine::Task<HRESULT> InsertBreakpoint(const DebugEngine::BreakpointSettings& settings);
		DebugEngine::Task<HRESULT> RunAndWait(std::string command, int flags = Capture);
		DebugEngine::Task<HRESULT> BuildTree(std::vector<DebugEngine::SymbolEntry>& entries, std::vector<std::pair<std::wstring, std::string>> roots, std::set<std::wstring> expanded);
		DebugEngine::Task<std::vector<MiValue>> ListChildren(std::string var);
		DebugEngine::Task<void> AddChildren(std::vector<DebugEngine::SymbolEntry>& entries, std::vector<std::pair<size_t, std::string>>& needPaths, std::vector<MiValue> children,
			std::wstring parentPath, ULONG depth, const std::set<std::wstring>& expanded);
		DebugEngine::Task<std::string> FunctionAt(ULONG64 address);
		std::vector<DebugEngine::EventFilter> ParseSignals(const std::string& text) const;

		GdbProcess m_Gdb;
		std::string m_GdbPath;
		NotificationHandler m_Handler;
		std::unique_ptr<DebugEngine::IDispatcher> m_Dispatcher;
		std::deque<PendingCommand> m_Pending;
		std::uint64_t m_NextToken{ 1 };

		DebugEngine::ExecutionStatus m_Status{ DebugEngine::ExecutionStatus::NoDebuggee };
		DebugEngine::TargetType m_TargetType{ DebugEngine::TargetType::Unknown };
		DebugEngine::TargetType m_NextTargetType{ DebugEngine::TargetType::LiveUser };
		bool m_HasTarget{ false };
		bool m_ExitPosted{ false };
		bool m_DownloadSymbols{ true };
		// MappedFile's: the file-backed regions (start, end, path) as of a stop of a process
		mutable std::vector<std::tuple<ULONG64, ULONG64, std::string>> m_Mapped;
		mutable std::pair<int, std::uint64_t> m_MappedAt{ 0, ~0ull };
		ULONG m_Thread{ 0 };			// GDB's number of the current thread
		ULONG m_Frame{ 0 };				// the scope frame
		int m_Pid{ 0 };					// the current inferior's process
		std::string m_ExePath;
		std::set<std::string> m_LiveGroups;
		std::map<ULONG, ULONG> m_Lwps;	// GDB's thread number -> LWP, as far as seen
		std::uint64_t m_StopGeneration{ 0 };
		std::vector<std::coroutine_handle<>> m_StopWaiters;
		std::set<ULONG> m_Internal;		// DebugX's own breakpoints (GDB never reuses a number)
		std::map<ULONG, DebugEngine::BreakpointSettings> m_BreakpointSettings;	// as they were given, by number
		std::vector<std::string> m_RegisterNames;
		std::vector<int> m_GeneralRegisters;	// the numbers of "info registers"' ones
		std::vector<DebugEngine::EventFilter> m_DefaultFilters;
		ULONG m_Radix{ 10 };
		int m_Busy{ 0 };
		std::wstring m_LastError;
	};
}
