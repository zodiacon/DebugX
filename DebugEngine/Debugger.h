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
		TargetChanged		// a target was attached or opened, or the session has ended
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
