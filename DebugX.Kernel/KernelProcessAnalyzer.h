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
		ULONG64 GetKernelTime() const;
		ULONG64 GetUserTime() const;
		ULONG GetThreadCount() const;
		ULONG GetSessionId() const;
		ULONG64 GetDirectoryTableBase() const;
		ULONG64 GetPeb() const;
		ULONG64 GetHandleTable() const;
		ULONG GetHandleCount() const;
		ULONG64 GetJob() const;

	private:
		KernelGlobals _globals;
		ULONG64 _processAddress = 0, _processEntry = 0;
	};
}

