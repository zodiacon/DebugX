#pragma once

#include <memory>
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
		ProcessCreated = 0x400,
		ThreadCreated,
		ModuleLoaded,
		ModuleUnloaded,
		Output,
		Exception,
		Breakpoint,
		ExitProcess,
		ExitThread,
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

	struct OutputData {
		OutputDataMask Mask;
		std::string Text;
	};

	class Debugger sealed {
	public:
		Debugger();
		~Debugger();

		HRESULT Create(DWORD notifyThreadId);
		HRESULT Create(HWND hWndNotify);

		HRESULT AttachToProcess(DWORD pid, AttachType attach);
		HRESULT RunAndAttachToProcess();
		HRESULT ExecuteCommand(const char* command);
		void FreeNotification(WPARAM id);

	private:
		struct Impl;
		std::unique_ptr<Impl> _impl;

	};
}

