#include "pch.h"
#include "KernelAnalyzerBase.h"
#include <unordered_map>
#include <string>

using namespace DebugX::Analysis;

using PairType = std::pair<ULONG, std::string>;

template<>
struct std::hash<PairType> {
	size_t operator()(const PairType& p) const {
		return p.first ^ std::hash<std::string>().operator()(p.second);
	}
};

KernelAnalyzerBase::KernelAnalyzerBase(IDebugClient* client) {
	_symbols = client;
	_client = client;
	_data = client;
	ATLASSERT(_symbols && _client && _data);

	_symbols->GetModuleByModuleName("nt", 0, nullptr, &_ntBase);
}

IDebugClient7* KernelAnalyzerBase::Client() const {
	return _client.p;
}

IDebugSymbols3* KernelAnalyzerBase::Symbols() const {
	return _symbols.p;
}

IDebugDataSpaces4* KernelAnalyzerBase::Data() const {
	return _data.p;
}

ULONG64 KernelAnalyzerBase::KernelBase() const {
	return _ntBase;
}

int KernelAnalyzerBase::GetFieldOffset(ULONG typeId, PCSTR name) const {
	static std::unordered_map<std::pair<ULONG, std::string>, ULONG> fieldMap;
	auto it = fieldMap.find({ typeId, name });
	if (it != fieldMap.end())
		return it->second;

	ULONG offset;
	auto hr = Symbols()->GetFieldOffset(KernelBase(), typeId, name, &offset);
	if (SUCCEEDED(hr)) {
		fieldMap.insert({ { typeId, name }, offset });
		return offset;
	}
	return -1;
}

ULONG KernelAnalyzerBase::GetTypeId(PCSTR name) const {
	static std::unordered_map<std::string, ULONG> typeIds;
	if (auto it = typeIds.find(name); it != typeIds.end())
		return it->second;

	ULONG id;
	auto hr = Symbols()->GetTypeId(KernelBase(), name, &id);
	if (SUCCEEDED(hr)) {
		typeIds.insert({ name, id });
		return id;
	}
	return 0;
}
