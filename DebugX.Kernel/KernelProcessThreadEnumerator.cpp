#include "pch.h"
#include "KernelProcessThreadEnumerator.h"
#include "KernelThreadAnalyzer.h"

using namespace DebugX::Analysis;

KernelProcessThreadEnumerator::KernelProcessThreadEnumerator(ULONG64 process, IDebugClient* client) : 
	KernelAnalyzerBase(client), _process(process) {
	auto offset = GetFieldOffset(GetTypeId("_EPROCESS"), "ThreadListHead");
	auto entry = ReadVirtual<LIST_ENTRY>(process + offset);
	_head = (ULONG64)entry.Flink;
	_threadAnalyzer = std::make_unique<KernelThreadAnalyzer>(_head - GetFieldOffset(GetTypeId("_ETHREAD"), "ThreadListEntry"), client);
	_head = process + offset;
}

KernelProcessThreadEnumerator::~KernelProcessThreadEnumerator() = default;

KernelThreadAnalyzer* KernelProcessThreadEnumerator::GetNextThread() {
	if(_threadAnalyzer->GetNextThreadLink() == _head)
		return nullptr;
	_threadAnalyzer->MoveToNextThread();
	return _threadAnalyzer.get();
}
