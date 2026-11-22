#include "pch.h"
#include "KernelProcessThreadEnumerator.h"
#include "KernelThreadAnalyzer.h"

using namespace DebugX::Analysis;

KernelProcessThreadEnumerator::KernelProcessThreadEnumerator(ULONG64 process, IDebugClient* client) : 
	KernelAnalyzerBase(client), m_Process(process) {
	// The analyzer starts on the list's head as if it were a thread's entry (as KernelProcessEnumerator does with the
	// process list), so the first GetNextThread moves to the first thread -- starting on the first thread skipped it.
	auto headOffset = GetFieldOffset(GetTypeId("_EPROCESS"), "ThreadListHead");
	auto entryOffset = GetFieldOffset(GetTypeId("_ETHREAD"), "ThreadListEntry");
	if (process && headOffset >= 0 && entryOffset >= 0)
		m_Head = process + headOffset;
	m_ThreadAnalyzer = std::make_unique<KernelThreadAnalyzer>(m_Head ? m_Head - entryOffset : 0, client);
}

KernelProcessThreadEnumerator::~KernelProcessThreadEnumerator() = default;

KernelThreadAnalyzer* KernelProcessThreadEnumerator::GetNextThread() {
	if (!m_Head || m_Count >= MaxThreads)
		return nullptr;
	auto link = m_ThreadAnalyzer->GetNextThreadLink();
	if (link == 0 || link == m_Head)
		return nullptr;
	m_Count++;
	m_ThreadAnalyzer->MoveToNextThread();
	return m_ThreadAnalyzer.get();
}
