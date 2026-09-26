#pragma once

namespace DebugX::Analysis {
	class KernelGlobals {
	public:
		KernelGlobals(IDebugSymbols* symbols);

		ULONG64 GetVariable(PCSTR name) const;

	private:
		CComQIPtr<IDebugSymbols> m_Symbols;
	};
}