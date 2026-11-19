#include "pch.h"
#include "KernelGlobals.h"
#include <string>
#include <unordered_map>

using namespace DebugX::Analysis;

KernelGlobals::KernelGlobals(IDebugSymbols* symbols) : m_Symbols(symbols) {
}

ULONG64 KernelGlobals::GetVariable(PCSTR name) const {
	// (by the kernel's base too: another target, or another boot, has them elsewhere)
	ULONG64 base = 0;
	m_Symbols->GetModuleByModuleName("nt", 0, nullptr, &base);
	static std::unordered_map<std::string, ULONG64> symbols;
	auto key = std::to_string(base) + ":" + name;
	if (auto it = symbols.find(key); it != symbols.end())
		return it->second;

	ULONG64 offset;
	if (FAILED(m_Symbols->GetOffsetByName(("nt!" + std::string(name)).c_str(), &offset)))
		offset = 0;
	symbols.insert({ key, offset });
	return offset;
}

