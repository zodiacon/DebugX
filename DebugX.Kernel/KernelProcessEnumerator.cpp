#include "pch.h"
#include "KernelProcessAnalyzer.h"
#include "KernelProcessEnumerator.h"

using namespace DebugX::Analysis;

KernelProcessEnumerator::KernelProcessEnumerator(IDebugClient* client) {
	CComQIPtr<IDebugSymbols3> spSymbols(client);
	ATLASSERT(spSymbols);

	auto hr = spSymbols->GetOffsetByName("nt!PsActiveProcessHead", &m_Head);
	ATLASSERT(SUCCEEDED(hr));

	m_Analyzer.reset(new KernelProcessAnalyzer(client));
	Reset();
}

KernelProcessEnumerator::~KernelProcessEnumerator() = default;

KernelProcessAnalyzer* KernelProcessEnumerator::GetNextProcess() {
	auto address = m_Analyzer->GetNextProcessLink();
	if (address == m_Head)
		return nullptr;
	
	m_Analyzer->MoveToNextProcess();
	return m_Analyzer.get();
}

void KernelProcessEnumerator::Reset() {
	m_Analyzer->SetProcessEntry(m_Head);
}
