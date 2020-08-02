#pragma once

#include <string>
#include <functional>

struct IDebugClient;

namespace DebugEngine {
	enum class AttachType {
		Default = 0,					// DEBUG_ATTACH_DEFAULT (invasive)
		NonInvasive = 1,				// DEBUG_ATTACH_NONINVASIVE
		Existing = 2,					// DEBUG_ATTACH_EXISTING
		NonInvasiveNoSuspend = 4,		// DEBUG_ATTACH_NONINVASIVE_NO_SUSPEND
		InvasiveNoInitialBreak = 8,		// DEBUG_ATTACH_INVASIVE_NO_INITIAL_BREAK
		InvasiveResumeProcess = 0x10,	// DEBUG_ATTACH_INVASIVE_RESUME_PROCESS
		NonInvasicAllowPartial = 0x20,	// DEBUG_ATTACH_NONINVASIVE_ALLOW_PARTIAL
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
		StatusChanged
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

	struct IMessageLoop {
		virtual void ProcessIdle() = 0;
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
	};

	class DebuggerX sealed {
	public:
		DebuggerX(IMessageLoop* msgLoop = nullptr);
		~DebuggerX();
		DebuggerX(const DebuggerX&) = delete;
		DebuggerX& operator=(const DebuggerX&) = delete;

		HRESULT Create(DWORD notifyThreadId);
		HRESULT Create(HWND hWndNotify);

		HRESULT OpenDumpFile(PCWSTR filename);
		HRESULT AttachToProcess(DWORD pid, AttachType attach = AttachType::Default);
		HRESULT LaunchAndAttach(PCWSTR exeName, PCWSTR commandLineArgs = nullptr, PCWSTR workingDir = nullptr, AttachType attach = AttachType::Default);
		HRESULT ExecuteCommand(PCWSTR command);
		HRESULT ExecuteCommand(PCSTR command);
		HRESULT AttachLocalKernel();
		HRESULT Break();
		HRESULT Stop();
		ExecutionStatus GetExecutionStatus();
		ExecutionStatus GetExecutionStatusAsync() const;
		TargetType GetTargetType() const;
		bool IsLocalKernelDebuggerAvailable() const;
		bool IsBusy() const;

		HRESULT OutputPrompt();

		HRESULT EnumModules(std::vector<ModuleInfo>& modules);
		HRESULT EnumProcesses(std::vector<ProcessInfo>& processes, std::vector<ThreadInfo>& threads);

		void FreeNotification(WPARAM id);

		// run code on the debugger's thread
		void RunAsync(std::function<void(IDebugClient*)> func);

		struct Impl;
	private:
		Impl* _impl;

	};
}

