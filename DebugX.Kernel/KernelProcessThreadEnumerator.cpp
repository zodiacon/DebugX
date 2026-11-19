#include "pch.h"
#include "KernelProcessThreadEnumerator.h"
#include "KernelThreadAnalyzer.h"

using namespace DebugX::Analysis;

KernelProcessThreadEnumerator::KernelProcessThreadEnumerator(ULONG64 process, IDebugClient* client) : 
	KernelAnalyzerBase(client), m_Process(process) {
	// The analyzer starts on the list's head as if it were a thread's entry (as KernelProcessEnumerator does with the
	// process list), so the first GetNextThread moves to the first thread -- starting on the first thread skipped it.
	m_Head = process + GetFieldOffset(GetTypeId("_EPROCESS"), "ThreadListHead");
	m_ThreadAnalyzer = std::make_unique<KernelThreadAnalyzer>(m_Head - GetFieldOffset(GetTypeId("_ETHREAD"), "ThreadListEntry"), client);
}

KernelProcessThreadEnumerator::~KernelProcessThreadEnumerator() = default;

KernelThreadAnalyzer* KernelProcessThreadEnumerator::GetNextThread() {
	if(m_ThreadAnalyzer->GetNextThreadLink() == m_Head)
		return nullptr;
	m_ThreadAnalyzer->MoveToNextThread();
	return m_ThreadAnalyzer.get();
}
