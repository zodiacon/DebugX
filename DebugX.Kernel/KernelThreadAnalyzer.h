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

		ULONG64 MoveToNextThread();

	private:
		ULONG64 _address;
	};
}

