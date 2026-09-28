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
		ContextChanged		// the current thread has changed, or its registers were written to (data: ContextData)
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

	struct StackFrameInfo {
		ULONG Index;
		ULONG64 InstructionOffset;
		ULONG64 ReturnOffset;
		ULONG64 FrameOffset;
		ULONG64 StackOffset;
		std::wstring Symbol;		// module!function+0xoffset; empty if there is no symbol
	};

	struct BreakpointInfo {
		ULONG Id;
		bool Enabled;
		bool Deferred;		// its address cannot be worked out yet: the module it is in is not loaded
		bool IsData;		// on access to memory rather than on running code
		ULONG64 Offset;
		ULONG PassCount;
		std::wstring Expression;
		std::wstring Command;
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
		Task<HRESULT> ExecuteCommand(PCWSTR command);
		Task<HRESULT> OutputPrompt();
		// Ends the session and terminates the target. A running target is interrupted first.
		Task<HRESULT> Stop();

		Task<HRESULT> EnumModules(std::vector<ModuleInfo>& modules);
		Task<HRESULT> EnumProcesses(std::vector<ProcessInfo>& processes, std::vector<ThreadInfo>& threads);

		// about the current thread of the target
		Task<HRESULT> GetRegisters(std::vector<RegisterInfo>& registers);
		Task<HRESULT> GetStackTrace(std::vector<StackFrameInfo>& frames, ULONG maxFrames = 128);
		// the thread by the engine's id (ThreadInfo::Id)
		Task<HRESULT> SetCurrentThread(ULONG threadId);

		Task<HRESULT> EnumBreakpoints(std::vector<BreakpointInfo>& breakpoints);
		// a breakpoint at an address or symbol, as the debugger takes them (module!function, 0x7ff6`1234, ...)
		Task<HRESULT> AddBreakpoint(PCWSTR expression);
		Task<HRESULT> RemoveBreakpoint(ULONG id);
		Task<HRESULT> SetBreakpointEnabled(ULONG id, bool enabled);

		// the current thread's instruction pointer
		Task<HRESULT> GetInstructionOffset(ULONG64& offset);
		// 'count' lines starting at 'address' (not necessarily aligned to an instruction boundary; the engine
		// resyncs). 'buffer' is resized to however many were actually decoded, which can be less than 'count'
		// (e.g. unreadable memory).
		Task<HRESULT> Disassemble(ULONG64 address, ULONG count, std::vector<DisassemblyLine>& lines);
		// a debugger expression (register, symbol, C-like arithmetic, ...), as typed at the command prompt
		Task<HRESULT> Evaluate(PCWSTR expression, ULONG64& value);
		// reads target memory; 'buffer' is resized to however many bytes were actually read (can be less than
		// requested at the end of a readable region, or empty if none of it is)
		Task<HRESULT> ReadMemory(ULONG64 address, std::vector<BYTE>& buffer, ULONG size);

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
