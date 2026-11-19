#pragma once

#include "KernelAnalyzerBase.h"

namespace DebugX::Analysis {
	struct ClientId {
		ULONG ProcessId, ThreadId;
	};

	class KernelThreadAnalyzer : public KernelAnalyzerBase {
	public:
		KernelThreadAnalyzer(ULONG64 address, IDebugClient* client);

		LARGE_INTEGER GetCreateTime() const;
		LARGE_INTEGER GetExitTime() const;
		ClientId GetClientId() const;
		ULONG64 GetNextThread() const;
		ULONG64 GetNextThreadLink() const;
		ULONG GetKernelTime() const;
		ULONG GetUserTime() const;
		// the ETHREAD
		ULONG64 GetAddress() const {
			return m_Address;
		}
		// where the thread started: Win32StartAddress (a user-mode thread's function), else StartAddress
		ULONG64 GetStartAddress() const;
		// _KTHREAD.State (KTHREAD_STATE: 0 Initialized, 1 Ready, 2 Running, 3 Standby, 4 Terminated, 5 Waiting, ...)
		UCHAR GetState() const;

		ULONG64 MoveToNextThread();

	private:
		ULONG64 m_Address;
	};
}

