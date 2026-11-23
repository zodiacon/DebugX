#pragma once

#include "IDebugger.h"

struct IDebugClient;

namespace DebugEngine {
	// Which dbgeng.dll the process uses (EngineLoader.cpp). An executable that delay-loads dbgeng.dll calls LoadEngine before
	// the first DebuggerX::Create: the dbgeng.dll then loaded is the one every DebuggerX uses.
	enum class EngineSource {
		// the Windows Kits debugger's, if it is installed (it has symsrv.dll, for symbol servers, and the extension DLLs
		// for the !-commands), else System32's
		Automatic,
		WindowsKits,
		System32,
		Folder,			// the one in the folder given
	};

	// the Windows Kits debugger's folder for this process's architecture (...\Debuggers\x64\), if it has a dbgeng.dll;
	// empty if not
	std::wstring WindowsKitsDebuggersFolder();
	// Loads the dbgeng.dll (with dbghelp.dll and the rest from its folder) and returns its full path; empty if it cannot be
	// loaded. 'folder': for EngineSource::Folder.
	std::wstring LoadEngine(EngineSource source, PCWSTR folder = nullptr);

	//
	// IDebugger on a DbgEng client (the documentation of the members is IDebugger's). The engine is bound to a thread of
	// its own, where calls are queued as APCs; a call such as g keeps it in WaitForEvent until the target breaks, and a
	// queued request waits its turn.
	//
	class DebuggerX final : public IDebugger {
	public:
		DebuggerX();
		~DebuggerX() override;
		DebuggerX(const DebuggerX&) = delete;
		DebuggerX& operator=(const DebuggerX&) = delete;

		// Starts the engine thread and creates the client
		HRESULT Create(NotificationHandler handler, std::unique_ptr<IDispatcher> dispatcher = nullptr) override;

		Task<HRESULT> OpenDumpFile(PCWSTR filename) override;
		Task<HRESULT> AttachToProcess(DWORD pid, AttachType attach = AttachType::Default) override;
		Task<HRESULT> LaunchAndAttach(PCWSTR exeName, PCWSTR commandLineArgs = nullptr, PCWSTR workingDir = nullptr, AttachType attach = AttachType::Default) override;
		Task<HRESULT> AttachLocalKernel() override;
		Task<HRESULT> AttachKernel(PCWSTR connectOptions) override;
		Task<HRESULT> ExecuteCommand(PCWSTR command) override;
		Task<HRESULT> ExecuteCommandCaptured(PCWSTR command, std::wstring& output) override;
		Task<HRESULT> OutputPrompt() override;
		Task<HRESULT> Stop() override;
		Task<HRESULT> Detach() override;
		// .detach / .kill
		Task<HRESULT> EndCurrentProcess(bool terminate) override;
		Task<HRESULT> SetDmlEnabled(bool enabled) override;

		Task<HRESULT> EnumModules(std::vector<ModuleInfo>& modules) override;
		Task<HRESULT> EnumProcesses(std::vector<ProcessInfo>& processes, std::vector<ThreadInfo>& threads) override;
		Task<HRESULT> EnumHandles(std::vector<HandleInfo>& handles) override;
		Task<HRESULT> SearchMemory(ULONG64 start, std::vector<BYTE> pattern, ULONG64& found, ULONG64 kernelRange = 0x10000000) override;
		// !analyze -v: the Windows Kits debugger's ext.dll; E_NOTIMPL with an engine that has no extensions. Symbols are loaded
		// (and downloaded).
		Task<HRESULT> Analyze(std::wstring& text) override;

		// dbgsrv -t npipe:pipe=X, or tcp:port=N, on the other machine; the options are what windbg -premote takes
		Task<HRESULT> ConnectProcessServer(PCWSTR options) override;
		Task<HRESULT> DisconnectProcessServer() override;
		Task<HRESULT> EnumServerProcesses(std::vector<ServerProcess>& processes) override;

		// JavaScript: the Windows Kits debugger's JsProvider (E_NOTIMPL with an engine that has no script provider). Run:
		// .scriptrun (loaded, and its invokeScript run); Load: .scriptload. A loaded script's functions can be called with
		// dx @$scriptContents.f().
		Task<HRESULT> Script(ScriptAction action, PCWSTR path, std::wstring& output) override;
		// (.scriptlist, without the engine's own NatVis files)
		Task<HRESULT> EnumScripts(std::vector<std::wstring>& paths) override;

		Task<HRESULT> GetRegisters(std::vector<RegisterInfo>& registers) override;
		Task<HRESULT> GetStackTrace(std::vector<StackFrameInfo>& frames, ULONG maxFrames = 128) override;
		Task<HRESULT> GetThreadStackTrace(ULONG threadId, std::vector<StackFrameInfo>& frames, ULONG maxFrames = 128) override;
		Task<HRESULT> SetCurrentThread(ULONG threadId) override;
		Task<HRESULT> SetCurrentProcess(ULONG processId) override;
		// ~f, ~u
		Task<HRESULT> SetThreadFrozen(ULONG threadId, bool frozen) override;
		// ~n, ~m
		Task<HRESULT> SuspendThread(ULONG threadId, bool suspend) override;
		// .childdbg for the session there is
		Task<HRESULT> SetDebugChildProcesses(bool debug) override;

		Task<HRESULT> SetScopeFrame(ULONG frameIndex) override;
		Task<HRESULT> GetScope(ULONG64& instructionOffset, ULONG& frameIndex) override;
		// (Locals and watches are the debugger's data model's, as dx shows them: a type with a visualizer -- NatVis, the
		// standard library's -- by what it shows, with its fields under [Raw View]; see ValueWalker.h)
		Task<HRESULT> GetLocals(std::vector<SymbolEntry>& entries, std::vector<std::wstring> expanded) override;
		Task<HRESULT> EvaluateWatches(std::vector<SymbolEntry>& entries, std::vector<std::wstring> expressions, std::vector<std::wstring> expanded) override;
		HRESULT SetDisplayRadix(ULONG radix) override;
		Task<HRESULT> AssignValue(PCWSTR expression, PCWSTR value, std::wstring& error) override;

		Task<HRESULT> EnumBreakpoints(std::vector<BreakpointInfo>& breakpoints) override;
		using IDebugger::AddBreakpoint;
		// (the engine has no conditions: DebugX keeps them, and evaluates them in the Breakpoint callback)
		Task<HRESULT> AddBreakpoint(BreakpointSettings settings) override;
		Task<HRESULT> UpdateBreakpoint(ULONG id, BreakpointSettings settings) override;
		Task<HRESULT> RemoveBreakpoint(ULONG id) override;
		Task<HRESULT> SetBreakpointEnabled(ULONG id, bool enabled) override;

		// 0x7ff61234; `file:line`
		std::wstring AddressLocation(ULONG64 address) const override;
		std::wstring SourceLineLocation(std::wstring_view file, ULONG line) const override;
		Task<HRESULT> ResolveLocation(PCWSTR location, ULONG64& address) override;
		// @$csp
		std::wstring StackPointerExpression() const override;

		// g; g <location>
		Task<HRESULT> Go() override;
		Task<HRESULT> RunTo(PCWSTR location) override;
		Task<HRESULT> GetInstructionOffset(ULONG64& offset) override;
		Task<HRESULT> Disassemble(ULONG64 address, ULONG count, std::vector<DisassemblyLine>& lines) override;
		Task<HRESULT> GetSourceLocations(std::vector<ULONG64> offsets, std::vector<SourceLocation>& locations) override;
		// (the engine's code level, l+t / l-t, which p and t typed in the Command view go by, is set for the step and restored)
		Task<HRESULT> Step(StepKind kind, bool bySourceLine) override;
		Task<HRESULT> SetInstructionOffset(ULONG64 offset, bool otherFunction = false) override;

		// as .sympath and .srcpath have them
		Task<HRESULT> GetSymbolPaths(std::wstring& symbolPath, std::wstring& sourcePath) override;
		Task<HRESULT> SetSymbolPath(PCWSTR path) override;
		Task<HRESULT> SetSourcePath(PCWSTR path) override;
		// .reload; with a module, .reload /f <module>
		Task<HRESULT> ReloadSymbols(PCWSTR module = nullptr) override;
		// User mode: a minidump, with all of the memory (and handles, threads...: .dump /ma) if 'full'. Kernel: a small or full one.
		Task<HRESULT> WriteDump(PCWSTR path, bool full) override;

		// .process /p /r (/i: invasive); .thread /p /r
		Task<HRESULT> SetKernelProcess(ULONG64 process, bool invasive = false) override;
		Task<HRESULT> GetKernelProcess(ULONG64& process) override;
		Task<HRESULT> SetKernelThread(ULONG64 thread) override;

		Task<HRESULT> Evaluate(PCWSTR expression, ULONG64& value) override;
		Task<HRESULT> ReadMemory(ULONG64 address, std::vector<BYTE>& buffer, ULONG size) override;
		Task<HRESULT> WriteMemory(ULONG64 address, std::vector<BYTE> data) override;
		Task<HRESULT> SetRegister(PCWSTR name, PCWSTR value) override;

		Task<HRESULT> GetEventFilters(std::vector<EventFilter>& filters, bool defaults = false) override;
		Task<HRESULT> SetEventFilters(std::vector<EventFilter> filters) override;

		// DbgEng only: runs code on the engine thread (the kernel analyzers, DebugX.Kernel, use the client). The function must
		// not use the client after it returns.
		Task<HRESULT> RunAsync(std::function<HRESULT(IDebugClient*)> func);

		HRESULT Break() override;

		[[nodiscard]] TargetType GetTargetType() const override;
		[[nodiscard]] bool IsBusy() const override;

		struct Impl;
	private:
		Impl* m_Impl;
	};
}
