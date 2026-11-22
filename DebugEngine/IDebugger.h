#pragma once

//
// The debugger as DebugX's windows use it, independent of the engine behind it: the types that go in and out of it, and
// IDebugger, the interface an engine implements (DebuggerX: DbgEng, Debugger.h).
//
// Nothing here is DbgEng's (no DbgEng.h, COM or DEBUG_* constants), so that another engine can implement it. It does use
// the Windows base types (ULONG, ULONG64, BYTE, HRESULT, PCWSTR, HANDLE, DEFINE_ENUM_FLAG_OPERATORS); a build for another
// platform supplies them. Some of the values (event filters, data breakpoint access, attach types, execution status) have
// the numbers DbgEng gives them, as DebugX saves some of them in its settings; DbgEng's implementation checks that they
// still do.
//

#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <functional>
#include "AsyncTask.h"
#include "Dispatcher.h"

namespace DebugEngine {
	enum class AttachType {
		Default = 					0,
		NonInvasive = 				1,
		Existing = 					2,
		NonInvasiveNoSuspend = 		4,
		InvasiveNoInitialBreak =	8,
		InvasiveResumeProcess =		0x10,
		NonInvasicAllowPartial =	0x20,
	};
	DEFINE_ENUM_FLAG_OPERATORS(DebugEngine::AttachType);

	// a thread or process that is not a particular one (BreakpointSettings::ThreadId: any)
	inline constexpr ULONG AnyId = 0xffffffff;

	// What an event filter does when its event happens (EventFilter::ExecutionOption)
	inline constexpr ULONG FilterBreak = 0;				// break in
	inline constexpr ULONG FilterSecondChanceBreak = 1;	// exceptions: break in if the target does not handle it
	inline constexpr ULONG FilterOutput = 2;			// only say so
	inline constexpr ULONG FilterIgnore = 3;
	// How the target goes on after an exception (EventFilter::ContinueOption)
	inline constexpr ULONG FilterGoHandled = 0;
	inline constexpr ULONG FilterGoNotHandled = 1;

	// What a data breakpoint breaks on (BreakpointSettings::DataAccess)
	inline constexpr ULONG BreakOnRead = 1;		// read or write
	inline constexpr ULONG BreakOnWrite = 2;
	inline constexpr ULONG BreakOnExecute = 4;

	enum class DebuggerNotificationType {
		ProcessCreated = 0xb00,
		ThreadCreated,
		ModuleLoaded,
		ModuleUnloaded,
		Output,
		Exception,
		Breakpoint,
		ExitProcess,
		ExitThread,
		StatusChanged,
		// no data for the two below: something the caller can ask the debugger about (IsBusy, GetTargetType) has changed
		BusyChanged,		// the first call is in flight, or the last one has completed
		TargetChanged,		// a target was attached or opened, or the session has ended
		BreakpointsChanged,	// a breakpoint was added, removed, enabled, disabled or changed (by whoever)
		ContextChanged,		// the current thread has changed, or its registers were written to (data: ContextData)
		ScopeChanged,		// the frame of the current thread's stack that locals and registers are about (data: ScopeData)
		MemoryChanged		// target memory was written to (WriteMemory, or a command such as eb); no data
	};

	// StatusChanged: the data is an ExecutionStatus
	enum class ExecutionStatus : ULONG {
		NoChange,
		Go,
		GoHandled,
		GoNotHandled,
		StepOver,
		StepInfo,
		Break,
		NoDebuggee,
		StepBranch,
		IgnoreEvent,
		RestartRequested,
		ReverseGo,
		ReverseStepBranch,
		ReverseStepOver,
		ReverseStepInto,
		OutOfSync,
		WaitInput,
		Timeout
	};

	enum class TargetType {
		Unknown = 0,
		User = 1,
		Kernel = 2,
		Live = 4,
		Dump = 8,
		LiveUser = Live | User,
		LiveKernel = Live | Kernel,
		LocalKernel = Kernel | 0x10,
		DumpUser = Dump | User,
		DumpKernel = Dump | Kernel
	};
	DEFINE_ENUM_FLAG_OPERATORS(DebugEngine::TargetType);

	struct ModuleInfo {
		ULONG64 BaseAddress;
		ULONG Size;
		ULONG Symbols;
		ULONG TimeStamp;
		std::wstring Name, ImageName, SymbolFile;
	};

	struct ThreadInfo {
		ULONG ThreadId;
		ULONG Id;
		ULONG64 Teb;
		HANDLE Handle;
		ULONG Pid;
		ULONG ProcessId{ 0 };		// the engine's id of its process (ProcessInfo::Id)
		std::wstring Name;			// its description (SetThreadDescription), if it has one
		ULONG64 InstructionOffset{ 0 };
		std::wstring Location;		// where it is: module!function+0x10
		ULONG SuspendCount{ 0 };	// as the engine reports it (a thread of a live target that is stopped counts the debugger's one)
		bool Frozen{ false };		// the debugger does not let it run when the target runs (SetThreadFrozen)
		bool Current{ false };		// the thread the engine's commands and the panes that show a thread are about
	};

	struct ProcessInfo {
		ULONG Id;
		ULONG ProcessId;
		ULONG64 Peb;
		HANDLE Handle;
		std::wstring ImagePath;
		ULONG UpTimeSec;
		ULONG ThreadCount{ 0 };
		bool Current{ false };		// the process the engine's commands and the panes are about
	};

	// a process of the machine the debugger attaches to: this one, or the process server's (EnumServerProcesses)
	struct ServerProcess {
		ULONG Pid{ 0 };
		std::wstring Name;
	};

	// a handle of the current process (EnumHandles)
	struct HandleInfo {
		ULONG64 Handle{ 0 };
		std::wstring Type;
		std::wstring Name;			// empty if it has none, or it is not asked for (see EnumHandles)
		ULONG GrantedAccess{ 0 };
		ULONG Attributes{ 0 };		// OBJ_INHERIT, OBJ_PROTECT_CLOSE (2, 1)
		ULONG64 Object{ 0 };		// the kernel object's address (0 if not known: not elevated)
	};

	struct CreateProcessData {
		void* BaseAddress;
		HANDLE hProcess;
		HANDLE hFile;
		HANDLE hThread;
		std::string ImageName;
		ULONG DateTimeStamp;
		ULONG Checksum;
		ULONG ModuleSize;
		void* ThreadDataOffset;
	};

	// ModuleLoaded and ModuleUnloaded
	struct ModuleEventData {
		ULONG64 BaseAddress;
		std::wstring Name;
	};

	// ContextChanged: the engine's id of the current thread
	struct ContextData {
		ULONG ThreadId;
	};

	// Exception: an exception in the target, before the engine's event filters decide whether it breaks
	struct ExceptionData {
		ULONG Code;
		ULONG64 Address;
		bool FirstChance;
		ULONG ThreadId;			// the system's
		std::wstring Name;		// the engine's name for the code ("Access violation"), if it has one
	};

	// An event filter of the engine: what it does when an event happens (a specific one: a thread created, a module
	// loaded, ...) or an exception is raised -- break, only say so, or ignore it -- and, for an exception, whether it
	// is passed on to the target as handled or not. What Debug > Exception Settings edits.
	struct EventFilter {
		std::wstring Name;			// "Access violation", "Create thread", ...; an exception added by code has none
		ULONG Code{ 0 };			// an exception's code
		bool IsException{ false };
		bool IsArbitrary{ false };	// an exception filter added by code (it can be removed), not one the engine has of its own
		ULONG ExecutionOption{ FilterSecondChanceBreak };	// FilterBreak / FilterSecondChanceBreak (exceptions) / FilterOutput / FilterIgnore
		ULONG ContinueOption{ FilterGoNotHandled };			// exceptions: FilterGoHandled / FilterGoNotHandled
	};

	// ScopeChanged: the index of the frame in the current thread's stack (0: the innermost, where the thread is)
	struct ScopeData {
		ULONG FrameIndex;
	};

	// A variable (or one of its members, elements, ...) in a tree of them, flattened: a row for each that can be seen,
	// in order, children after their parent. Path identifies it across refreshes (its names from the root, separated
	// by '\n'); what is expanded is asked for by path, so a row's place in the list is never needed to find it.
	struct SymbolEntry {
		std::wstring Path;
		std::wstring Name;
		std::wstring Type;
		std::wstring Value;		// or why there is none (an expression that does not evaluate)
		// a C++ expression for it that can be assigned to (AssignValue): a number, a pointer, ... that has one; else empty
		std::wstring Expression;
		ULONG Depth{ 0 };
		bool Expandable{ false };
		bool Expanded{ false };
		bool Error{ false };
	};

	struct RegisterInfo {
		std::wstring Name;
		std::wstring Value;		// as the value is usually written: hexadecimal for integers, decimal for floating point
	};

	// one line of Disassemble output (as the engine formats it: address, opcode bytes, mnemonic) and the address
	// it is for -- from the engine's own end of the previous line, not parsed back out of the text
	struct DisassemblyLine {
		ULONG64 Address;
		std::wstring Text;
	};

	// where an address is in the source (GetSourceLocations)
	struct SourceLocation {
		std::wstring File;		// the source file as the symbols name it (where it was compiled); empty: no line information
		std::wstring Path;		// the file on this machine (found through the source path, or File itself); empty: not found
		ULONG Line{ 0 };		// 1-based
	};

	// SetInstructionOffset: the address is in another function
	inline constexpr HRESULT SetNextOtherFunction = MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x201);

	enum class StepKind {
		Into,
		Over,
		Out
	};

	struct StackFrameInfo {
		ULONG Index;
		ULONG64 InstructionOffset;
		ULONG64 ReturnOffset;
		ULONG64 FrameOffset;
		ULONG64 StackOffset;
		std::wstring Symbol;		// module!function+0xoffset; empty if there is no symbol
	};

	// What a breakpoint is to be (AddBreakpoint, UpdateBreakpoint).
	struct BreakpointSettings {
		// code: where -- a location in the engine's syntax (module!function, an address, a source line: see
		// IDebugger::AddressLocation, SourceLineLocation; it can be in a module that is not loaded yet);
		// data: the address of the memory (an expression, worked out when the breakpoint is set)
		std::wstring Expression;
		// a C++ expression (locals, members, ...), evaluated in the thread that hits the breakpoint: it breaks only if
		// the value is not zero. Empty: always.
		std::wstring Condition;
		std::wstring Command;			// debugger commands run when it breaks
		ULONG PassCount{ 1 };			// it breaks on this hit, and every one after it (1: every time)
		ULONG ThreadId{ AnyId };		// the engine's id of the only thread it breaks in (ThreadInfo::Id), or any
		bool Enabled{ true };
		bool OneShot{ false };			// removed when it is hit
		bool IsData{ false };			// on access to memory rather than on running code
		ULONG DataSize{ 1 };			// data: 1, 2, 4 or 8 bytes, aligned to their size
		ULONG DataAccess{ BreakOnWrite };	// data: BreakOnWrite, BreakOnRead (read or write), BreakOnExecute
	};

	struct BreakpointInfo : BreakpointSettings {
		ULONG Id;
		bool Deferred;		// its address cannot be worked out yet: the module it is in is not loaded
		ULONG64 Offset;
		ULONG Hits{ 0 };	// the times it broke into the debugger (with its condition true), as far as DebugX has seen
		// where it is in a way that holds in another session (an address does not: the program is loaded elsewhere):
		// the expression it was set with, or else a source line if it is where a line starts, or module!function+0x10
		std::wstring Location;
	};

	struct ExitThreadData {
		ULONG ExitCode;
		ULONG Id;
		ULONG ThreadId;
	};

	// Breakpoint: the id of the breakpoint that broke. It may not be one of EnumBreakpoints': the engine's own (the one
	// a step out or a run to an address runs to) come too.
	struct BreakpointData {
		ULONG Id;
	};

	enum class OutputDataMask {
		None				= 0,
		Normal				= 1,
		Error				= 2,
		Warning				= 4,
		Verbose				= 8,
		Prompt				= 0x10,
		PromptRegisters		= 0x20,
		ExtensionWarning	= 0x40,
		Debuggee			= 0x80,
		DebuggePrompt		= 0x100,
		Symbols				= 0x200,
		Status				= 0x400,
		Xml					= 0x800,
	};
	DEFINE_ENUM_FLAG_OPERATORS(DebugEngine::OutputDataMask);

	struct OutputData {
		OutputDataMask Mask;
		std::wstring Text;
		bool IsDml : 1;
	};

	struct ThreadCreateData {
		HANDLE Handle;
		ULONG64 Teb;
		ULONG64 StartAddress;
		ULONG ThreadId;
		ULONG Id;
		ULONG Pid;
	};

	//
	// A debugger engine. Everything that talks to the engine runs on a thread of the implementation's and the calling
	// thread is never blocked: the calls below return a Task that is co_await'ed (or dropped, see AsyncTask.h) and resume
	// on the calling thread, through the dispatcher given to Create. Engine events are delivered to the notification
	// handler on that thread as well, so the caller never has to synchronize with the engine.
	//
	// Create, the destructor and every member that is not a Task must be used on the thread that created the debugger,
	// except Break, which is safe anywhere. Arguments that are pointers or references (the out parameters of Enum*) must
	// stay valid until the Task completes; strings are copied.
	//
	// What an engine cannot do fails with E_NOTIMPL (the members with a body here do that by default): a kernel target,
	// scripts, a process server, ...
	//
	class IDebugger {
	public:
		// 'data' is only valid for the duration of the call. Its type depends on 'type' (see the *Data structs above).
		using NotificationHandler = std::function<void(DebuggerNotificationType type, void* data)>;

		virtual ~IDebugger() = default;

		// Starts the engine and its thread. The completions of the calls below and the notifications are run through
		// 'dispatcher', on the thread it belongs to (the calling one); none: CreateWindowDispatcher's, on this thread,
		// which must then have a message loop.
		virtual HRESULT Create(NotificationHandler handler, std::unique_ptr<IDispatcher> dispatcher = nullptr) = 0;

		virtual Task<HRESULT> OpenDumpFile(PCWSTR filename) = 0;
		virtual Task<HRESULT> AttachToProcess(DWORD pid, AttachType attach = AttachType::Default) = 0;
		virtual Task<HRESULT> LaunchAndAttach(PCWSTR exeName, PCWSTR commandLineArgs = nullptr, PCWSTR workingDir = nullptr, AttachType attach = AttachType::Default) = 0;
		virtual Task<HRESULT> AttachLocalKernel() {
			co_return E_NOTIMPL;
		}
		// a kernel debugging connection, as windbg -k takes it: "net:port=50000,key=a.b.c.d", "com:port=COM1,baud=115200", ...
		// The task completes once the target has connected -- not before: nothing interrupts DbgEng's wait for it.
		virtual Task<HRESULT> AttachKernel(PCWSTR connectOptions) {
			co_return E_NOTIMPL;
		}
		// a command in the engine's own language, as typed in the Command view; its output comes as Output notifications
		virtual Task<HRESULT> ExecuteCommand(PCWSTR command) = 0;
		virtual Task<HRESULT> OutputPrompt() = 0;
		// Ends the session and terminates the target. A running target is interrupted first.
		virtual Task<HRESULT> Stop() = 0;
		// Ends the session and leaves the target running. A running target is interrupted first.
		virtual Task<HRESULT> Detach() = 0;
		// Ends the debugging of the current process (SetCurrentProcess) only: lets it go, or terminates it ('terminate')
		virtual Task<HRESULT> EndCurrentProcess(bool terminate) = 0;
		// whether the engine's output is marked up (DML) where the command can produce it
		virtual Task<HRESULT> SetDmlEnabled(bool enabled) {
			co_return E_NOTIMPL;
		}

		virtual Task<HRESULT> EnumModules(std::vector<ModuleInfo>& modules) = 0;
		virtual Task<HRESULT> EnumProcesses(std::vector<ProcessInfo>& processes, std::vector<ThreadInfo>& threads) = 0;
		// The handles of the current process (a live user-mode target on this computer only: E_NOTIMPL otherwise). The name of
		// a file that is not a disk file is not asked for: for a synchronous pipe with a read waiting, that would never return.
		virtual Task<HRESULT> EnumHandles(std::vector<HandleInfo>& handles) {
			co_return E_NOTIMPL;
		}
		// The first place at or after 'start' where the bytes are: in user mode, in the committed memory of the current
		// process up to its end; in kernel mode, in the next 'kernelRange' bytes. HRESULT_FROM_WIN32(ERROR_NOT_FOUND): none
		virtual Task<HRESULT> SearchMemory(ULONG64 start, std::vector<BYTE> pattern, ULONG64& found, ULONG64 kernelRange = 0x10000000) = 0;
		// An analysis of the target (a dump, or why it stopped), as text: DbgEng's !analyze -v. It can take a while.
		virtual Task<HRESULT> Analyze(std::wstring& text) {
			co_return E_NOTIMPL;
		}

		// A process server on another machine: from then on AttachToProcess and LaunchAndAttach are about its processes (a
		// path on that machine), until DisconnectProcessServer -- this debugger does the rest, with its own symbols. The
		// options are the engine's (DbgEng: what windbg -premote takes, "npipe:server=host,pipe=X", "tcp:server=host,port=N").
		// Not while there is a target.
		virtual Task<HRESULT> ConnectProcessServer(PCWSTR options) {
			co_return E_NOTIMPL;
		}
		virtual Task<HRESULT> DisconnectProcessServer() {
			co_return E_NOTIMPL;
		}
		// the processes that can be attached to: the process server's, or this machine's
		virtual Task<HRESULT> EnumServerProcesses(std::vector<ServerProcess>& processes) = 0;

		// Debugger scripts. Run: loaded, and its entry point run; Load: loaded only. Either way it stays -- its functions
		// can be called -- until Unload. 'output' is what the engine said, which goes to the Output notifications as well; a
		// script that throws, or does not load, fails.
		enum class ScriptAction { Run, Load, Unload };
		virtual Task<HRESULT> Script(ScriptAction action, PCWSTR path, std::wstring& output) {
			co_return E_NOTIMPL;
		}
		// the scripts that are loaded (their paths)
		virtual Task<HRESULT> EnumScripts(std::vector<std::wstring>& paths) {
			co_return E_NOTIMPL;
		}

		// about the current thread of the target; the registers are as they were in the current scope frame
		// (only the nonvolatile ones are reliable in a frame other than 0)
		virtual Task<HRESULT> GetRegisters(std::vector<RegisterInfo>& registers) = 0;
		virtual Task<HRESULT> GetStackTrace(std::vector<StackFrameInfo>& frames, ULONG maxFrames = 128) = 0;
		// the thread by the engine's id (ThreadInfo::Id)
		virtual Task<HRESULT> SetCurrentThread(ULONG threadId) = 0;
		// the process by the engine's id (ProcessInfo::Id); its current thread becomes the current one. ContextChanged follows
		virtual Task<HRESULT> SetCurrentProcess(ULONG processId) = 0;
		// Freezes a thread (the engine's id), or thaws it: a frozen thread does not run when the target does
		virtual Task<HRESULT> SetThreadFrozen(ULONG threadId, bool frozen) {
			co_return E_NOTIMPL;
		}
		// suspends a thread (one more suspend count), or resumes it (one less)
		virtual Task<HRESULT> SuspendThread(ULONG threadId, bool suspend) {
			co_return E_NOTIMPL;
		}
		// whether the processes a program starts are debugged too (by a launch from now on, and by the session there is);
		// off by default
		virtual Task<HRESULT> SetDebugChildProcesses(bool debug) {
			co_return E_NOTIMPL;
		}

		// the frame of the current thread's stack (StackFrameInfo::Index) that locals, registers and expressions are
		// about; it goes back to 0 whenever the target stops or the current thread changes
		virtual Task<HRESULT> SetScopeFrame(ULONG frameIndex) = 0;
		// where the current scope frame is executing, and its index
		virtual Task<HRESULT> GetScope(ULONG64& instructionOffset, ULONG& frameIndex) = 0;
		// the arguments and locals of the current scope frame; 'expanded' has the paths (SymbolEntry::Path) to expand
		virtual Task<HRESULT> GetLocals(std::vector<SymbolEntry>& entries, std::vector<std::wstring> expanded) = 0;
		// a tree for each expression (in the current scope); one that does not evaluate is an Error entry
		virtual Task<HRESULT> EvaluateWatches(std::vector<SymbolEntry>& entries, std::vector<std::wstring> expressions, std::vector<std::wstring> expanded) = 0;
		// Assigns the value (a C++ expression) to what the expression (SymbolEntry::Expression) is, in the current scope;
		// 'error' says why it could not be. MemoryChanged follows, delivered before the task completes.
		virtual Task<HRESULT> AssignValue(PCWSTR expression, PCWSTR value, std::wstring& error) = 0;

		virtual Task<HRESULT> EnumBreakpoints(std::vector<BreakpointInfo>& breakpoints) = 0;
		virtual Task<HRESULT> AddBreakpoint(BreakpointSettings settings) = 0;
		// a breakpoint at a location (see BreakpointSettings::Expression)
		Task<HRESULT> AddBreakpoint(PCWSTR location) {
			BreakpointSettings settings;
			settings.Expression = location;
			return AddBreakpoint(std::move(settings));
		}
		// a breakpoint made over (a code one cannot become a data one, or the other way around: E_INVALIDARG)
		virtual Task<HRESULT> UpdateBreakpoint(ULONG id, BreakpointSettings settings) = 0;
		virtual Task<HRESULT> RemoveBreakpoint(ULONG id) = 0;
		virtual Task<HRESULT> SetBreakpointEnabled(ULONG id, bool enabled) = 0;

		// Code locations in the engine's syntax, for AddBreakpoint, RunTo and ResolveLocation: an address, and a line of a
		// source file (the file as the symbols name it, or a path)
		[[nodiscard]] virtual std::wstring AddressLocation(ULONG64 address) const = 0;
		[[nodiscard]] virtual std::wstring SourceLineLocation(std::wstring_view file, ULONG line) const = 0;
		// the address of the code at a location
		virtual Task<HRESULT> ResolveLocation(PCWSTR location, ULONG64& address) = 0;
		// an expression for the current thread's stack pointer, whatever the processor (Evaluate)
		[[nodiscard]] virtual std::wstring StackPointerExpression() const = 0;

		// Lets the target run (the task completes when it stops again, or the session ends)
		virtual Task<HRESULT> Go() = 0;
		// Lets the target run until the current thread reaches the location (AddressLocation, SourceLineLocation, ...), or
		// something else stops it first
		virtual Task<HRESULT> RunTo(PCWSTR location) = 0;
		// the current thread's instruction pointer
		virtual Task<HRESULT> GetInstructionOffset(ULONG64& offset) = 0;
		// 'count' lines starting at 'address' (not necessarily aligned to an instruction boundary; the engine
		// resyncs). 'lines' is resized to however many were actually decoded, which can be less than 'count'
		// (e.g. unreadable memory).
		virtual Task<HRESULT> Disassemble(ULONG64 address, ULONG count, std::vector<DisassemblyLine>& lines) = 0;
		// the source file and line of each address, in the same order (an address without line information gets an empty one)
		virtual Task<HRESULT> GetSourceLocations(std::vector<ULONG64> offsets, std::vector<SourceLocation>& locations) = 0;
		// Steps the current thread (and waits for the step to end): by a source line where the thread is in code that has
		// line information and 'bySourceLine' is set, else by an instruction. What the engine's own step commands (typed in
		// the Command view) go by is left as it was.
		virtual Task<HRESULT> Step(StepKind kind, bool bySourceLine) = 0;
		// Set Next Statement: moves the current thread's instruction pointer (the target must be stopped); ContextChanged
		// follows. An address in another function than the one the thread is in is refused (SetNextOtherFunction) unless
		// 'otherFunction' is set: the stack is then not what that code expects.
		virtual Task<HRESULT> SetInstructionOffset(ULONG64 offset, bool otherFunction = false) = 0;

		// The engine's symbol and source paths, in its syntax (DbgEng: as .sympath and .srcpath have them,
		// "srv*C:\Symbols*https://...;C:\src"). They are the engine's, not a session's: they carry over from one target to the next.
		virtual Task<HRESULT> GetSymbolPaths(std::wstring& symbolPath, std::wstring& sourcePath) = 0;
		virtual Task<HRESULT> SetSymbolPath(PCWSTR path) = 0;
		virtual Task<HRESULT> SetSourcePath(PCWSTR path) = 0;
		// The symbols of the modules are looked up again (each when it is next needed); with a module (its image name,
		// kernel32.dll): that one's are loaded now. ContextChanged follows for a stopped target, so that what shows symbols
		// catches up.
		virtual Task<HRESULT> ReloadSymbols(PCWSTR module = nullptr) = 0;
		// A dump file of a live target that is stopped (E_NOTIMPL for a dump, E_UNEXPECTED while it runs); with all of the
		// memory if 'full'.
		virtual Task<HRESULT> WriteDump(PCWSTR path, bool full) {
			co_return E_NOTIMPL;
		}

		// Kernel targets. The process (its EPROCESS) whose user-mode memory and modules the debugger looks at.
		// 'invasive' (a live kernel over a connection only, E_NOTIMPL otherwise): the target runs until that process is the
		// current one, and breaks in (the task completes then). ContextChanged follows.
		virtual Task<HRESULT> SetKernelProcess(ULONG64 process, bool invasive = false) {
			co_return E_NOTIMPL;
		}
		// the process the debugger looks at (the implicit process)
		virtual Task<HRESULT> GetKernelProcess(ULONG64& process) {
			co_return E_NOTIMPL;
		}
		// The thread (its ETHREAD) whose registers and stack the debugger looks at. ContextChanged follows.
		// A local kernel has no thread contexts to switch to (it fails).
		virtual Task<HRESULT> SetKernelThread(ULONG64 thread) {
			co_return E_NOTIMPL;
		}

		// an expression (register, symbol, C-like arithmetic, ...) in the engine's syntax, as typed at the command prompt
		virtual Task<HRESULT> Evaluate(PCWSTR expression, ULONG64& value) = 0;
		// reads target memory; 'buffer' is resized to however many bytes were actually read (can be less than
		// requested at the end of a readable region, or empty if none of it is)
		virtual Task<HRESULT> ReadMemory(ULONG64 address, std::vector<BYTE>& buffer, ULONG size) = 0;
		// writes target memory (all of it, or it fails: ERROR_PARTIAL_COPY if only some could be); MemoryChanged follows,
		// delivered before the task completes
		virtual Task<HRESULT> WriteMemory(ULONG64 address, std::vector<BYTE> data) = 0;
		// a register of the current thread (by name, as GetRegisters has it), from text: an expression for an integer one
		// (hexadecimal by default, as GetRegisters shows them), a number for a floating-point one. Vector registers are not
		// supported (E_NOTIMPL). ContextChanged follows.
		virtual Task<HRESULT> SetRegister(PCWSTR name, PCWSTR value) = 0;

		// The engine's event filters: the specific events, then the exceptions (its own, then the ones added by code).
		// 'defaults': as they were when the engine was created, before anybody changed them.
		virtual Task<HRESULT> GetEventFilters(std::vector<EventFilter>& filters, bool defaults = false) = 0;
		// Sets the filters given (a specific event or exception the engine has by its name or code); the exceptions added
		// by code become exactly the IsArbitrary ones given -- one that is not in the list is removed.
		virtual Task<HRESULT> SetEventFilters(std::vector<EventFilter> filters) = 0;

		// Interrupts a running target.
		virtual HRESULT Break() = 0;

		// (The execution status comes with the StatusChanged notifications, in order with the other events.)
		[[nodiscard]] virtual TargetType GetTargetType() const = 0;
		// True while any of the calls above is still in flight.
		[[nodiscard]] virtual bool IsBusy() const = 0;
	};
}
