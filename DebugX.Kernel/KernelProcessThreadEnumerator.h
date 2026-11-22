#pragma once

#include "KernelAnalyzerBase.h"

namespace DebugX::Analysis {
	class KernelThreadAnalyzer;

	class KernelProcessThreadEnumerator : public KernelAnalyzerBase {
	public:
		KernelProcessThreadEnumerator(ULONG64 process, IDebugClient* client);
		~KernelProcessThreadEnumerator();

		// null at the end of the list -- also where a link cannot be read, the kernel's symbols do not have the list
		// (_EPROCESS.ThreadListHead, _ETHREAD.ThreadListEntry), or it goes on past MaxThreads (a list that does not end)
		KernelThreadAnalyzer* GetNextThread();
		static constexpr ULONG MaxThreads = 100000;

	private:
		ULONG64 m_Process;
		ULONG64 m_Head{ 0 };
		ULONG m_Count{ 0 };
		std::unique_ptr<KernelThreadAnalyzer> m_ThreadAnalyzer;
	};
}

