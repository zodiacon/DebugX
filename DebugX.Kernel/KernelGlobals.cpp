#include "pch.h"
#include "KernelGlobals.h"
#include <string>
#include <unordered_map>

using namespace DebugX::Analysis;

KernelGlobals::KernelGlobals(IDebugSymbols* symbols) : _symbols(symbols) {
}

ULONG64 KernelGlobals::GetVariable(PCSTR name) const {
	static std::unordered_map<std::string, ULONG64> symbols;
	if (auto it = symbols.find(name); it != symbols.end())
		return it->second;

	ULONG64 offset;
	auto hr = _symbols->GetOffsetByName(("nt!" + std::string(name)).c_str(), &offset);
	if (SUCCEEDED(hr)) {
		symbols.insert({ name, offset });
		return offset;
	}
	return 0;
}

