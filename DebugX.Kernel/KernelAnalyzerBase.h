#pragma once

namespace DebugX::Analysis {
	class KernelAnalyzerBase abstract {
	public:
		// -1 if the type has no such field (or the type is 0)
		int GetFieldOffset(ULONG typeId, PCSTR name) const;
		// 0 if the kernel's symbols have no such type
		ULONG GetTypeId(PCSTR type) const;
		ULONG64 KernelBase() const;

		// whether the kernel's symbols are loaded (a PDB, not only its exports, or none yet -- as on a kernel over a
		// connection before they have come): a lookup that fails without them may well succeed later
		static bool HasKernelSymbols(IUnknown* symbols, ULONG64 base);		// (any IDebugSymbols*)
		bool HasKernelSymbols() const;

		template<typename T>
		T ReadVirtual(ULONG64 address, HRESULT* phr = nullptr) const;

	protected:
		KernelAnalyzerBase(IDebugClient* client);

		IDebugClient7* Client() const;
		IDebugSymbols3* Symbols() const;
		IDebugDataSpaces4* Data() const;

	private:
		ULONG64 m_NtBase{ 0 };
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

