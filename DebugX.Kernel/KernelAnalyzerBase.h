#pragma once

namespace DebugX::Analysis {
	class KernelAnalyzerBase abstract {
	public:
		int GetFieldOffset(ULONG typeId, PCSTR name) const;
		ULONG GetTypeId(PCSTR type) const;
		ULONG64 KernelBase() const;

		template<typename T>
		T ReadVirtual(ULONG64 address, HRESULT* phr = nullptr) const;

	protected:
		KernelAnalyzerBase(IDebugClient* client);

		IDebugClient7* Client() const;
		IDebugSymbols3* Symbols() const;
		IDebugDataSpaces4* Data() const;

	private:
		ULONG64 m_NtBase;
		CComQIPtr<IDebugClient7> m_Client;
		CComQIPtr<IDebugSymbols3> m_Symbols;
		CComQIPtr<IDebugDataSpaces4> m_Data;
	};

	template<typename T>
	inline T KernelAnalyzerBase::ReadVirtual(ULONG64 address, HRESULT* phr) const {
		T value{};
		auto hr = m_Data->ReadVirtual(address, &value, sizeof(value), nullptr);
		if (phr)
			*phr = hr;
		return value;
	}
}

