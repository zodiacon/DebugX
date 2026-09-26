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
		ULONG64 m_Process;
		ULONG64 m_Head;
		std::unique_ptr<KernelThreadAnalyzer> m_ThreadAnalyzer;
	};
}

