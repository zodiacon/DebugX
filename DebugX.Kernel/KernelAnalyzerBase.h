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
		ULONG64 _ntBase;
		CComQIPtr<IDebugClient7> _client;
		CComQIPtr<IDebugSymbols3> _symbols;
		CComQIPtr<IDebugDataSpaces4> _data;
	};

	template<typename T>
	inline T KernelAnalyzerBase::ReadVirtual(ULONG64 address, HRESULT* phr) const {
		T value{};
		auto hr = _data->ReadVirtual(address, &value, sizeof(value), nullptr);
		if (phr)
			*phr = hr;
		return value;
	}
}

