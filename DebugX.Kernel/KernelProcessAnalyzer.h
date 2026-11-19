#pragma once

#include "KernelAnalyzerBase.h"
#include "KernelGlobals.h"

namespace DebugX::Analysis {
	class KernelProcessThreadEnumerator;

	class KernelProcessAnalyzer : public KernelAnalyzerBase {
	public:
		KernelProcessAnalyzer(IDebugClient* pClient);
		~KernelProcessAnalyzer();

		bool SetProcess(ULONG64 processAddress);
		bool SetProcessEntry(ULONG64 processEntry);
		ULONG64 MoveToNextProcess();
		std::unique_ptr<KernelProcessThreadEnumerator> GetThreadEnumerator() const;

		// attributes
		ULONG64 GetProcessAddress() const;
		ULONG64 GetNextProcessLink() const;
		ULONG GetProcessId() const;
		CString GetImageName() const;
		CString GetFullImageName() const;
		LARGE_INTEGER GetCreateTime() const;
		LARGE_INTEGER GetExitTime() const;
		ULONG64 GetKernelTime() const;
		ULONG64 GetUserTime() const;
		ULONG GetThreadCount() const;
		// InvalidSessionId if it cannot be known (public symbols lack the session's type) or the process has none
		ULONG GetSessionId() const;
		static constexpr ULONG InvalidSessionId = ~0UL;
		ULONG64 GetDirectoryTableBase() const;
		ULONG64 GetPeb() const;
		ULONG64 GetHandleTable() const;
		ULONG GetHandleCount() const;
		ULONG64 GetJob() const;

	private:
		KernelGlobals m_Globals;
		ULONG64 m_ProcessAddress = 0, m_ProcessEntry = 0;
	};
}

