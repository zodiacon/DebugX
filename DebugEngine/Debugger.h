#pragma once

#include <string>

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
		Vreak,
		NoDebugge,
		SteoBranch,
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
		std::string Text;
	};

	class Debugger sealed {
	public:
		Debugger();
		~Debugger();
		Debugger(const Debugger&) = delete;
		Debugger& operator=(const Debugger&) = delete;

		HRESULT Create(DWORD notifyThreadId);
		HRESULT Create(HWND hWndNotify);

		HRESULT AttachToProcess(DWORD pid, AttachType attach = AttachType::Default);
		HRESULT LaunchAndAttach(PCWSTR exeName, PCWSTR commandLineArgs = nullptr, AttachType attach = AttachType::Default);
		HRESULT ExecuteCommand(PCWSTR command);
		HRESULT ExecuteCommand(PCSTR command);
		HRESULT AttachLocalKernel();
		HRESULT Break();
		ExecutionStatus GetExecutionStatus();

		HRESULT OutputPrompt();

		void FreeNotification(WPARAM id);

	private:
		struct Impl;
		Impl* _impl;

	};
}

