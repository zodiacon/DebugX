#include "pch.h"
#include "KernelProcessAnalyzer.h"
#include "KernelProcessEnumerator.h"

using namespace DebugX::Analysis;

KernelProcessEnumerator::KernelProcessEnumerator(IDebugClient* client) {
	CComQIPtr<IDebugSymbols3> spSymbols(client);
	ATLASSERT(spSymbols);

	m_Analyzer.reset(new KernelProcessAnalyzer(client));
	if (FAILED(spSymbols->GetOffsetByName("nt!PsActiveProcessHead", &m_Head)) || !m_Analyzer->CanWalk())
		m_Head = 0;
	Reset();
}

KernelProcessEnumerator::~KernelProcessEnumerator() = default;

KernelProcessAnalyzer* KernelProcessEnumerator::GetNextProcess() {
	if (!m_Head)
		return nullptr;

	auto address = m_Analyzer->GetNextProcessLink();
	if (address == m_Head)
		return nullptr;
	if (address == 0 || m_Count >= MaxProcesses) {
		m_Complete = false;
		return nullptr;
	}

	m_Count++;
	m_Analyzer->MoveToNextProcess();
	return m_Analyzer.get();
}

void KernelProcessEnumerator::Reset() {
	m_Count = 0;
	m_Complete = true;
	m_Analyzer->SetProcessEntry(m_Head);
}

bool KernelProcessEnumerator::IsValid() const {
	return m_Head != 0;
}

bool KernelProcessEnumerator::IsComplete() const {
	return m_Complete;
}
