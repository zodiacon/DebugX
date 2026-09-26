#include "pch.h"
#include "KernelProcessThreadEnumerator.h"
#include "KernelThreadAnalyzer.h"

using namespace DebugX::Analysis;

KernelProcessThreadEnumerator::KernelProcessThreadEnumerator(ULONG64 process, IDebugClient* client) : 
	KernelAnalyzerBase(client), m_Process(process) {
	auto offset = GetFieldOffset(GetTypeId("_EPROCESS"), "ThreadListHead");
	auto entry = ReadVirtual<LIST_ENTRY>(process + offset);
	m_Head = (ULONG64)entry.Flink;
	m_ThreadAnalyzer = std::make_unique<KernelThreadAnalyzer>(m_Head - GetFieldOffset(GetTypeId("_ETHREAD"), "ThreadListEntry"), client);
	m_Head = process + offset;
}

KernelProcessThreadEnumerator::~KernelProcessThreadEnumerator() = default;

KernelThreadAnalyzer* KernelProcessThreadEnumerator::GetNextThread() {
	if(m_ThreadAnalyzer->GetNextThreadLink() == m_Head)
		return nullptr;
	m_ThreadAnalyzer->MoveToNextThread();
	return m_ThreadAnalyzer.get();
}
