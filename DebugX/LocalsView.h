#pragma once

#include "SymbolPane.h"
#include "resource.h"

// The arguments and local variables of the scope frame (the call stack's chosen frame; 0 when the target stops).
class CLocalsView : public CSymbolPane<CLocalsView> {
public:
	BEGIN_MSG_MAP(CLocalsView)
		CHAIN_MSG_MAP(CSymbolPane<CLocalsView>)
	END_MSG_MAP()

	DebugEngine::Task<HRESULT> Fetch(std::vector<DebugEngine::SymbolEntry>& entries) {
		return DebugManager::DebuggerX().GetLocals(entries, ExpandedPaths());
	}

	void OnTargetEnded() override {
		m_Expanded.clear();
		SetEntries({});
		MarkDirty();
	}
};
