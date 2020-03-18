#pragma once

namespace DebugX::Analysis {
	class EProcessAnalyzer {
	public:
		EProcessAnalyzer(ULONG64 nt, IDebugClient* pClient);

		bool SetProcess(ULONG64 processAddress);
		bool SetProcessEntry(ULONG64 processEntry);
		ULONG64 MoveToNextProcess();
		void Refresh();

		// attributes
		ULONG64 GetProcessAddress() const;
		ULONG GetProcessId() const;
		CString GetImageName() const;
		CString GetFullImageName() const;
		LARGE_INTEGER GetCreateTime() const;
		LARGE_INTEGER GetKernelTime() const;
		LARGE_INTEGER GetUserTimer() const;

		const void* GetData(ULONG offset) const;
		const void* GetData(PCSTR field) const;

	private:
		ULONG64 _ntAddress;
		ULONG64 _processAddress = 0, _processEntry = 0;
		CComQIPtr<IDebugSymbols3> _symbols;
		CComQIPtr<IDebugDataSpaces> _data;
		BYTE _buffer[1 << 12];
	};
}

