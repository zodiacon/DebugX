#include "pch.h"
#include "KernelProcessAnalyzer.h"
#include "KernelProcessEnumerator.h"

using namespace DebugX::Analysis;

KernelProcessEnumerator::KernelProcessEnumerator(IDebugClient* client) {
	CComQIPtr<IDebugSymbols3> spSymbols(client);
	ATLASSERT(spSymbols);

	auto hr = spSymbols->GetOffsetByName("nt!PsActiveProcessHead", &_head);
	ATLASSERT(SUCCEEDED(hr));

	_analyzer.reset(new KernelProcessAnalyzer(client));
	Reset();
}

KernelProcessEnumerator::~KernelProcessEnumerator() = default;

KernelProcessAnalyzer* KernelProcessEnumerator::GetNextProcess() {
	auto address = _analyzer->GetNextProcessLink();
	if (address == _head)
		return nullptr;
	
	_analyzer->MoveToNextProcess();
	return _analyzer.get();
}

void KernelProcessEnumerator::Reset() {
	_analyzer->SetProcessEntry(_head);
}
