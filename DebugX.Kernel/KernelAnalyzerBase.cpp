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
	m_Symbols = client;
	m_Client = client;
	m_Data = client;
	ATLASSERT(m_Symbols && m_Client && m_Data);

	m_Symbols->GetModuleByModuleName("nt", 0, nullptr, &m_NtBase);
}

IDebugClient7* KernelAnalyzerBase::Client() const {
	return m_Client.p;
}

IDebugSymbols3* KernelAnalyzerBase::Symbols() const {
	return m_Symbols.p;
}

IDebugDataSpaces4* KernelAnalyzerBase::Data() const {
	return m_Data.p;
}

ULONG64 KernelAnalyzerBase::KernelBase() const {
	return m_NtBase;
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
