#pragma once

namespace DebugEngine {
	enum class AttachType {
		Default = 0,					// DEBUG_ATTACH_DEFAULT
		NonInvasive = 1,				// DEBUG_ATTACH_NONINVASIVE
		Existing = 2,					// DEBUG_ATTACH_EXISTING
		NonInvasiveNoSuspend = 4,		// DEBUG_ATTACH_NONINVASIVE_NO_SUSPEND
		InvasiveNoInitialBreak = 8,		// DEBUG_ATTACH_INVASIVE_NO_INITIAL_BREAK
		InvasiveResumeProcess = 0x10,	// DEBUG_ATTACH_INVASIVE_RESUME_PROCESS
		NonInvasicAllowPartial = 0x20,	// DEBUG_ATTACH_NONINVASIVE_ALLOW_PARTIAL
	};
	DEFINE_ENUM_FLAG_OPERATORS(DebugEngine::AttachType);

	class Debugger sealed {
	public:
		Debugger();
		~Debugger();

		HRESULT Create();

		HRESULT AttachToProcess(DWORD pid, AttachType attach);
		HRESULT RunAndAttachToProcess();

	private:
		struct Impl;
		std::unique_ptr<Impl> _impl;

	};
}

