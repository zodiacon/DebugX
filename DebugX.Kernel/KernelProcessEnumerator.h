#pragma once

namespace DebugX::Analysis {
	class KernelProcessAnalyzer;

	class KernelProcessEnumerator {
	public:
		KernelProcessEnumerator(IDebugClient* client);
		~KernelProcessEnumerator();

		// null at the end of the list -- also where it cannot be followed (see IsComplete)
		KernelProcessAnalyzer* GetNextProcess();
		void Reset();

		// whether the kernel's symbols have the list (nt!PsActiveProcessHead, _EPROCESS.ActiveProcessLinks): not, as a
		// rule, before they are loaded (a kernel over a connection, at first); then there are no processes
		bool IsValid() const;
		// whether the list was followed back to its head: not if a link could not be read, or it went on past
		// MaxProcesses (a list that does not end -- wrong offsets, memory that changed under a live system)
		bool IsComplete() const;
		static constexpr ULONG MaxProcesses = 100000;

	private:
		std::unique_ptr<KernelProcessAnalyzer> m_Analyzer;
		ULONG64 m_Head{ 0 };
		ULONG m_Count{ 0 };
		bool m_Complete{ true };
	};
}
