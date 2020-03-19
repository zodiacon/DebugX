#pragma once

#include "KernelAnalyzerBase.h"

namespace DebugX::Analysis {
	class KernelThreadAnalyzer;

	class KernelProcessThreadEnumerator : public KernelAnalyzerBase {
	public:
		KernelProcessThreadEnumerator(ULONG64 process, IDebugClient* client);
		~KernelProcessThreadEnumerator();

		KernelThreadAnalyzer* GetNextThread();

	private:
		ULONG64 _process;
		ULONG64 _head;
		std::unique_ptr<KernelThreadAnalyzer> _threadAnalyzer;
	};
}

