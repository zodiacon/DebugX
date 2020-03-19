#pragma once

namespace DebugX::Analysis {
	class KernelProcessAnalyzer;

	class KernelProcessEnumerator {
	public:
		KernelProcessEnumerator(IDebugClient* client);
		~KernelProcessEnumerator();

		KernelProcessAnalyzer* GetNextProcess();
		void Reset();

	private:
		std::unique_ptr<KernelProcessAnalyzer> _analyzer;
		ULONG64 _head;
	};
}
