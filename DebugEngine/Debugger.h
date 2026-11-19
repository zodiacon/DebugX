#pragma once

#include <string>
#include <vector>
#include <functional>
#include "AsyncTask.h"

struct IDebugClient;

namespace DebugEngine {
	enum class AttachType {
		Default = 					DEBUG_ATTACH_DEFAULT,
		NonInvasive = 				DEBUG_ATTACH_NONINVASIVE,
		Existing = 					DEBUG_ATTACH_EXISTING,
		NonInvasiveNoSuspend = 		DEBUG_ATTACH_NONINVASIVE_NO_SUSPEND,
		InvasiveNoInitialBreak =	DEBUG_ATTACH_INVASIVE_NO_INITIAL_BREAK,
		InvasiveResumeProcess =		DEBUG_ATTACH_INVASIVE_RESUME_PROCESS,
		NonInvasicAllowPartial =	DEBUG_ATTACH_NONINVASIVE_ALLOW_PARTIAL,
	};
	DEFINE_ENUM_FLAG_OPERATORS(DebugEngine::AttachType);

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

	enum class ExecutionStatus {
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
		bool Current{ false };		// the thread the engine's commands and the panes that show a thread are about
	};

	struct ProcessInfo {
		ULONG Id;
		ULONG ProcessId;
		ULONG64 Peb;
		HANDLE Handle;
		std::wstring ImagePath;
		ULONG UpTimeSec;
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
		ULONG ExecutionOption{ DEBUG_FILTER_SECOND_CHANCE_BREAK };	// DEBUG_FILTER_BREAK / SECOND_CHANCE_BREAK (exceptions) / OUTPUT / IGNORE
		ULONG ContinueOption{ DEBUG_FILTER_GO_NOT_HANDLED };		// exceptions: DEBUG_FILTER_GO_HANDLED / GO_NOT_HANDLED
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
	// it is for -- from the engine's own EndOffset of the previous line, not parsed back out of the text
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
		Into,	// t
		Over,	// p
		Out		// gu
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
		// code: where (module!function, 0x7ff6`1234, ...; it can be in a module that is not loaded yet);
		// data: the address of the memory (an expression, worked out when the breakpoint is set)
		std::wstring Expression;
		// a C++ expression (locals, members, ...), evaluated in the thread that hits the breakpoint: it breaks only if
		// the value is not zero. Empty: always. (DebugX's own: the engine has no conditions -- see the Breakpoint callback)
		std::wstring Condition;
		std::wstring Command;			// debugger commands run when it breaks
		ULONG PassCount{ 1 };			// it breaks on this hit, and every one after it (1: every time)
		ULONG ThreadId{ DEBUG_ANY_ID };	// the engine's id of the only thread it breaks in (ThreadInfo::Id), or any
		bool Enabled{ true };
		bool OneShot{ false };			// removed when it is hit
		bool IsData{ false };			// on access to memory rather than on running code
		ULONG DataSize{ 1 };			// data: 1, 2, 4 or 8 bytes, aligned to their size
		ULONG DataAccess{ DEBUG_BREAK_WRITE };	// data: DEBUG_BREAK_WRITE, DEBUG_BREAK_READ (read or write), DEBUG_BREAK_EXECUTE
	};

	struct BreakpointInfo : BreakpointSettings {
		ULONG Id;
		bool Deferred;		// its address cannot be worked out yet: the module it is in is not loaded
		ULONG64 Offset;
		ULONG Hits{ 0 };	// the times it broke into the debugger (with its condition true), as far as DebugX has seen
	};

	struct ExitThreadData {
		ULONG ExitCode;
		ULONG Id;
		ULONG ThreadId;
	};

	struct BreakpointData {
		IDebugBreakpoint* Breakpoint;
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
	// Wraps a DbgEng client. The engine is bound to a thread of its own; everything that talks to it runs there and
	// the calling thread is never blocked: the calls below return a Task that is co_await'ed (or dropped, see
	// AsyncTask.h) and resume on the calling thread, from its message loop. Engine callbacks are queued and delivered
	// to the notification handler on that thread as well, so the caller never has to synchronize with the engine.
	//
	// Create, the destructor and every member that is not a Task must be used on the thread that created the debugger,
	// except Break, which is safe anywhere. Arguments that are pointers or references (the
	// out parameters of Enum*) must stay valid until the Task completes.
	//
	class DebuggerX final {
	public:
		// 'data' is only valid for the duration of the call. Its type depends on 'type' (see the *Data structs above).
		using NotificationHandler = std::function<void(DebuggerNotificationType type, void* data)>;

		DebuggerX();
		~DebuggerX();
		DebuggerX(const DebuggerX&) = delete;
		DebuggerX& operator=(const DebuggerX&) = delete;

		// Starts the engine thread and creates the client; the calling thread must have a message loop.
		HRESULT Create(NotificationHandler handler);

		Task<HRESULT> OpenDumpFile(PCWSTR filename);
		Task<HRESULT> AttachToProcess(DWORD pid, AttachType attach = AttachType::Default);
		Task<HRESULT> LaunchAndAttach(PCWSTR exeName, PCWSTR commandLineArgs = nullptr, PCWSTR workingDir = nullptr, AttachType attach = AttachType::Default);
		Task<HRESULT> AttachLocalKernel();
		// a kernel debugging connection, as windbg -k takes it: "net:port=50000,key=a.b.c.d", "com:port=COM1,baud=115200", ...
		// The task completes once the target has connected (Break gives up waiting).
		Task<HRESULT> AttachKernel(PCWSTR connectOptions);
		Task<HRESULT> ExecuteCommand(PCWSTR command);
		Task<HRESULT> OutputPrompt();
		// Ends the session and terminates the target. A running target is interrupted first.
		Task<HRESULT> Stop();
		// Ends the session and leaves the target running. A running target is interrupted first.
		Task<HRESULT> Detach();
		// whether the engine's output is in DML (Debugger Markup Language) where the command can produce it
		Task<HRESULT> SetDmlEnabled(bool enabled);

		Task<HRESULT> EnumModules(std::vector<ModuleInfo>& modules);
		Task<HRESULT> EnumProcesses(std::vector<ProcessInfo>& processes, std::vector<ThreadInfo>& threads);

		// about the current thread of the target; the registers are as they were in the current scope frame
		// (only the nonvolatile ones are reliable in a frame other than 0)
		Task<HRESULT> GetRegisters(std::vector<RegisterInfo>& registers);
		Task<HRESULT> GetStackTrace(std::vector<StackFrameInfo>& frames, ULONG maxFrames = 128);
		// the thread by the engine's id (ThreadInfo::Id)
		Task<HRESULT> SetCurrentThread(ULONG threadId);

		// the frame of the current thread's stack (StackFrameInfo::Index) that locals, registers and expressions are
		// about; it goes back to 0 whenever the target stops or the current thread changes
		Task<HRESULT> SetScopeFrame(ULONG frameIndex);
		// where the current scope frame is executing, and its index
		Task<HRESULT> GetScope(ULONG64& instructionOffset, ULONG& frameIndex);
		// the arguments and locals of the current scope frame; 'expanded' has the paths (SymbolEntry::Path) to expand
		Task<HRESULT> GetLocals(std::vector<SymbolEntry>& entries, std::vector<std::wstring> expanded);
		// a tree for each expression (in the current scope); one that does not evaluate is an Error entry
		Task<HRESULT> EvaluateWatches(std::vector<SymbolEntry>& entries, std::vector<std::wstring> expressions, std::vector<std::wstring> expanded);

		Task<HRESULT> EnumBreakpoints(std::vector<BreakpointInfo>& breakpoints);
		// a breakpoint at an address or symbol, as the debugger takes them (module!function, 0x7ff6`1234, ...)
		Task<HRESULT> AddBreakpoint(PCWSTR expression);
		Task<HRESULT> AddBreakpoint(BreakpointSettings settings);
		// a breakpoint made over (a code one cannot become a data one, or the other way around: E_INVALIDARG)
		Task<HRESULT> UpdateBreakpoint(ULONG id, BreakpointSettings settings);
		Task<HRESULT> RemoveBreakpoint(ULONG id);
		Task<HRESULT> SetBreakpointEnabled(ULONG id, bool enabled);

		// the current thread's instruction pointer
		Task<HRESULT> GetInstructionOffset(ULONG64& offset);
		// 'count' lines starting at 'address' (not necessarily aligned to an instruction boundary; the engine
		// resyncs). 'buffer' is resized to however many were actually decoded, which can be less than 'count'
		// (e.g. unreadable memory).
		Task<HRESULT> Disassemble(ULONG64 address, ULONG count, std::vector<DisassemblyLine>& lines);
		// the source file and line of each address, in the same order (an address without line information gets an empty one)
		Task<HRESULT> GetSourceLocations(std::vector<ULONG64> offsets, std::vector<SourceLocation>& locations);
		// Steps the current thread (and waits for the step to end): by a source line where the thread is in code that has
		// line information and 'bySourceLine' is set, else by an instruction. The engine's own code level (l+t / l-t,
		// which p and t typed in the Command view go by) is left as it was.
		Task<HRESULT> Step(StepKind kind, bool bySourceLine);
		// Set Next Statement: moves the current thread's instruction pointer (the target must be stopped); ContextChanged
		// follows. An address in another function than the one the thread is in is refused (SetNextOtherFunction) unless
		// 'otherFunction' is set: the stack is then not what that code expects.
		Task<HRESULT> SetInstructionOffset(ULONG64 offset, bool otherFunction = false);

		// The engine's symbol and source paths, as .sympath and .srcpath have them ("srv*C:\Symbols*https://...;C:\src").
		// They are the engine's, not a session's: they carry over from one target to the next.
		Task<HRESULT> GetSymbolPaths(std::wstring& symbolPath, std::wstring& sourcePath);
		Task<HRESULT> SetSymbolPath(PCWSTR path);
		Task<HRESULT> SetSourcePath(PCWSTR path);
		// .reload: the symbols of the modules are looked up again (each when it is next needed); with a module (its image
		// name, kernel32.dll), .reload /f: that one's are loaded now. ContextChanged follows for a stopped target, so that
		// what shows symbols catches up.
		Task<HRESULT> ReloadSymbols(PCWSTR module = nullptr);
		// A dump file of a live target that is stopped (E_NOTIMPL for a dump, E_UNEXPECTED while it runs). User mode: a
		// minidump, with all of the memory (and handles, threads...: .dump /ma) if 'full'. Kernel: a small or full one.
		Task<HRESULT> WriteDump(PCWSTR path, bool full);

		// Kernel targets. The process (its EPROCESS) whose user-mode memory and modules the debugger looks at: .process /p /r.
		// 'invasive' (a live kernel over a connection only, E_NOTIMPL otherwise): .process /i, the target runs until that
		// process is the current one, and breaks in (the task completes then). ContextChanged follows.
		Task<HRESULT> SetKernelProcess(ULONG64 process, bool invasive = false);
		// the process the debugger looks at (the implicit process)
		Task<HRESULT> GetKernelProcess(ULONG64& process);
		// The thread (its ETHREAD) whose registers and stack the debugger looks at: .thread /p /r. ContextChanged follows.
		// A local kernel has no thread contexts to switch to (it fails).
		Task<HRESULT> SetKernelThread(ULONG64 thread);

		// a debugger expression (register, symbol, C-like arithmetic, ...), as typed at the command prompt
		Task<HRESULT> Evaluate(PCWSTR expression, ULONG64& value);
		// reads target memory; 'buffer' is resized to however many bytes were actually read (can be less than
		// requested at the end of a readable region, or empty if none of it is)
		Task<HRESULT> ReadMemory(ULONG64 address, std::vector<BYTE>& buffer, ULONG size);
		// writes target memory (all of it, or it fails: ERROR_PARTIAL_COPY if only some could be); MemoryChanged follows,
		// delivered before the task completes
		Task<HRESULT> WriteMemory(ULONG64 address, std::vector<BYTE> data);
		// a register of the current thread (by name, as GetRegisters has it), from text: an expression for an integer one
		// (in the engine's radix, hexadecimal by default, as GetRegisters shows them), a number for a floating-point one.
		// Vector registers are not supported (E_NOTIMPL). ContextChanged follows.
		Task<HRESULT> SetRegister(PCWSTR name, PCWSTR value);

		// The engine's event filters: the specific events, then the exceptions (its own, then the ones added by code).
		// 'defaults': as they were when the engine was created, before anybody changed them.
		Task<HRESULT> GetEventFilters(std::vector<EventFilter>& filters, bool defaults = false);
		// Sets the filters given (a specific event or exception the engine has by its name or code); the exceptions added
		// by code become exactly the IsArbitrary ones given -- one that is not in the list is removed.
		Task<HRESULT> SetEventFilters(std::vector<EventFilter> filters);

		// Runs code on the engine thread. The function must not use the client after it returns.
		Task<HRESULT> RunAsync(std::function<HRESULT(IDebugClient*)> func);

		// Interrupts a running target.
		HRESULT Break();

		// (The execution status comes with the StatusChanged notifications, in order with the other events.)
		[[nodiscard]] TargetType GetTargetType() const;
		// True while any of the calls above is still in flight.
		[[nodiscard]] bool IsBusy() const;

		struct Impl;
	private:
		Impl* m_Impl;
	};
}
