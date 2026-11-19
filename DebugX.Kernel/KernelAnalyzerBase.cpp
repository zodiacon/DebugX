#include "pch.h"
#include "KernelAnalyzerBase.h"
#include <unordered_map>
#include <string>

using namespace DebugX::Analysis;

namespace {
	// A lookup the symbols answer is remembered -- one they cannot answer as well: a type or field the kernel's PDB does
	// not have (public symbols lack some) is looked for through all of them each time, which is slow, and a walk of the
	// processes asks for each one. Keyed by the kernel's base too: another target can have another kernel.
	struct Key {
		ULONG64 Base;
		ULONG TypeId;
		std::string Name;
		bool operator==(const Key& other) const {
			return Base == other.Base && TypeId == other.TypeId && Name == other.Name;
		}
	};
	struct KeyHash {
		size_t operator()(const Key& k) const {
			return std::hash<ULONG64>()(k.Base) ^ (std::hash<ULONG>()(k.TypeId) << 1) ^ std::hash<std::string>()(k.Name);
		}
	};
}

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
	// (-1: no such field, or no such type -- 0)
	static std::unordered_map<Key, int, KeyHash> offsets;
	Key key{ KernelBase(), typeId, name };
	if (auto it = offsets.find(key); it != offsets.end())
		return it->second;

	ULONG offset;
	int result = typeId != 0 && SUCCEEDED(Symbols()->GetFieldOffset(KernelBase(), typeId, name, &offset)) ? static_cast<int>(offset) : -1;
	offsets.insert({ std::move(key), result });
	return result;
}

ULONG KernelAnalyzerBase::GetTypeId(PCSTR name) const {
	// (0: no such type)
	static std::unordered_map<Key, ULONG, KeyHash> typeIds;
	Key key{ KernelBase(), 0, name };
	if (auto it = typeIds.find(key); it != typeIds.end())
		return it->second;

	ULONG id;
	if (FAILED(Symbols()->GetTypeId(KernelBase(), name, &id)))
		id = 0;
	typeIds.insert({ std::move(key), id });
	return id;
}
