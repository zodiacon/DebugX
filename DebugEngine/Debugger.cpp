#include "pch.h"
#include "Debugger.h"
#include <atomic>
#include <format>
#include <functional>
#include <mutex>
#include <set>
#include <atlcom.h>
#include <wil\resource.h>
#include <DbgModel.h>
//#include <DbgModelClientEx.h>

#pragma comment(lib, "dbgeng")

using namespace DebugEngine;

DebuggerX::Impl* g_Debugger;

#define HR(x) { auto _hr = (x); if(FAILED(_hr)) return _hr; }

namespace {
	// Both are posted to the message window that the debugger creates on the thread that created it.
	// WM_DBG_NOTIFY: engine callbacks are waiting in the notification queue
	// WM_DBG_RESUME: an engine call has completed, lParam is its AsyncOpData
	constexpr UINT WM_DBG_NOTIFY = WM_APP + 0x500;
	constexpr UINT WM_DBG_RESUME = WM_APP + 0x501;
	constexpr PCWSTR WindowClassName = L"DebugEngine.MessageWindow";

	// Carries a suspended coroutine's state across the hop from the debug thread (where the queued engine call
	// runs) back to the thread that awaited it. Ownership passes to EngineAwaiter::await_resume(), which deletes it.
	struct AsyncOpData {
		std::function<HRESULT()> Action;
		HRESULT Result{ E_FAIL };
		std::coroutine_handle<> Handle;
	};

	struct Notification {
		DebuggerNotificationType Type;
		void* Data;
	};

	void FreeNotification(DebuggerNotificationType type, void* data) {
		switch (type) {
			case DebuggerNotificationType::ProcessCreated: delete static_cast<CreateProcessData*>(data); break;
			case DebuggerNotificationType::ThreadCreated: delete static_cast<ThreadCreateData*>(data); break;
			case DebuggerNotificationType::Output: delete static_cast<OutputData*>(data); break;
			case DebuggerNotificationType::Breakpoint: delete static_cast<BreakpointData*>(data); break;
			case DebuggerNotificationType::ExitProcess:
			case DebuggerNotificationType::ExitThread: delete static_cast<ExitThreadData*>(data); break;
			case DebuggerNotificationType::StatusChanged: delete static_cast<ULONG*>(data); break;
			case DebuggerNotificationType::ModuleLoaded:
			case DebuggerNotificationType::ModuleUnloaded: delete static_cast<ModuleEventData*>(data); break;
			case DebuggerNotificationType::ContextChanged: delete static_cast<ContextData*>(data); break;
			case DebuggerNotificationType::ScopeChanged: delete static_cast<ScopeData*>(data); break;
			default: break;
		}
	}

	// a register's value the way it is usually written
	std::wstring FormatValue(IDebugControl7* control, const DEBUG_VALUE& value) {
		switch (value.Type) {
			case DEBUG_VALUE_INT8: return std::format(L"{:02X}", value.I8);
			case DEBUG_VALUE_INT16: return std::format(L"{:04X}", value.I16);
			case DEBUG_VALUE_INT32: return std::format(L"{:08X}", value.I32);
			case DEBUG_VALUE_INT64: return std::format(L"{:016X}", value.I64);
			case DEBUG_VALUE_FLOAT32: return std::format(L"{:g}", value.F32);
			case DEBUG_VALUE_FLOAT64: return std::format(L"{:g}", value.F64);
			case DEBUG_VALUE_FLOAT80:
			case DEBUG_VALUE_FLOAT82:
			case DEBUG_VALUE_FLOAT128: {
				DEBUG_VALUE in(value), out{};
				if (SUCCEEDED(control->CoerceValue(&in, DEBUG_VALUE_FLOAT64, &out)))
					return std::format(L"{:g}", out.F64);
				break;
			}
			case DEBUG_VALUE_VECTOR64: return std::format(L"{:08X} {:08X}", value.VI32[1], value.VI32[0]);
			case DEBUG_VALUE_VECTOR128: return std::format(L"{:08X} {:08X} {:08X} {:08X}", value.VI32[3], value.VI32[2], value.VI32[1], value.VI32[0]);
		}
		return L"?";
	}

	bool IsExecuting(ULONG status) {
		return status == DEBUG_STATUS_GO || status == DEBUG_STATUS_GO_HANDLED || status == DEBUG_STATUS_GO_NOT_HANDLED ||
			status == DEBUG_STATUS_STEP_OVER || status == DEBUG_STATUS_STEP_INTO || status == DEBUG_STATUS_STEP_BRANCH;
	}
}

struct DebuggerX::Impl : CComObjectRoot,
	IDebugEventCallbacks,
	IDebugOutputCallbacksWide,
	IDebugOutputCallbacks2 {

	BEGIN_COM_MAP(Impl)
		COM_INTERFACE_ENTRY(IDebugOutputCallbacksWide)
		COM_INTERFACE_ENTRY(IDebugOutputCallbacks2)
		COM_INTERFACE_ENTRY(IDebugEventCallbacks)
	END_COM_MAP()

	Impl();
	~Impl();

	IDataModelManager* GetDataManager() {
		return m_spDataMgr.p;
	}

	IDebugHost* GetDebugHost() {
		return m_spDebugHost.p;
	}

	HRESULT Create(NotificationHandler handler);
	// Detaches from (or ends the session with) the target and stops the debug thread. False if the engine did not
	// respond, in which case the thread is still running and the object must be left alone.
	bool Shutdown();

	Task<HRESULT> OpenDumpFile(PCWSTR filename);
	Task<HRESULT> AttachToProcess(DWORD pid, AttachType attach);
	Task<HRESULT> LaunchAndAttach(PCWSTR exeName, PCWSTR commandLineArgs, PCWSTR workingDir, AttachType attach);
	Task<HRESULT> AttachLocalKernel();
	Task<HRESULT> AttachKernel(PCWSTR connectOptions);
	Task<HRESULT> ExecuteCommand(PCWSTR command);
	Task<HRESULT> OutputPrompt();
	Task<HRESULT> Stop();
	Task<HRESULT> Detach();
	Task<HRESULT> SetDmlEnabled(bool enabled);
	Task<HRESULT> EnumModules(std::vector<ModuleInfo>& modules);
	Task<HRESULT> EnumProcesses(std::vector<ProcessInfo>& processes, std::vector<ThreadInfo>& threads);
	Task<HRESULT> RunAsync(std::function<HRESULT(IDebugClient*)> func);
	Task<HRESULT> GetRegisters(std::vector<RegisterInfo>& registers);
	Task<HRESULT> GetStackTrace(std::vector<StackFrameInfo>& frames, ULONG maxFrames);
	Task<HRESULT> SetCurrentThread(ULONG threadId);
	Task<HRESULT> SetScopeFrame(ULONG frameIndex);
	Task<HRESULT> GetScope(ULONG64& instructionOffset, ULONG& frameIndex);
	Task<HRESULT> GetLocals(std::vector<SymbolEntry>& entries, std::vector<std::wstring> expanded);
	Task<HRESULT> EvaluateWatches(std::vector<SymbolEntry>& entries, std::vector<std::wstring> expressions, std::vector<std::wstring> expanded);
	Task<HRESULT> EnumBreakpoints(std::vector<BreakpointInfo>& breakpoints);
	Task<HRESULT> AddBreakpoint(PCWSTR expression);
	Task<HRESULT> RemoveBreakpoint(ULONG id);
	Task<HRESULT> SetBreakpointEnabled(ULONG id, bool enabled);
	Task<HRESULT> GetInstructionOffset(ULONG64& offset);
	Task<HRESULT> Disassemble(ULONG64 address, ULONG count, std::vector<DisassemblyLine>& lines);
	Task<HRESULT> Evaluate(PCWSTR expression, ULONG64& value);
	Task<HRESULT> ReadMemory(ULONG64 address, std::vector<BYTE>& buffer, ULONG size);
	HRESULT Break();

	TargetType GetTargetType() const {
		return m_TargetType;
	}

	bool IsBusy() const {
		return m_PendingOps > 0;
	}

private:
	// Awaiter that queues an engine call to the debug thread and suspends the calling coroutine until it has run.
	// The coroutine is resumed on the calling thread by the message window (see OnResume).
	struct EngineAwaiter {
		Impl* Self;
		std::function<HRESULT()> Action;
		AsyncOpData* Data{};

		bool await_ready() const noexcept {
			return false;
		}

		bool await_suspend(std::coroutine_handle<> handle) {
			Data = new AsyncOpData{ std::move(Action), E_FAIL, handle };
			if (Self->Queue([self = Self, data = Data] {
				data->Result = data->Action();
				::PostMessageW(self->m_hWnd, WM_DBG_RESUME, 0, reinterpret_cast<LPARAM>(data));
				})) {
				// (the completion is handled on this thread, so it cannot come before this)
				if (++Self->m_PendingOps == 1)
					Self->PostNotification(DebuggerNotificationType::BusyChanged, nullptr);
				return true;
			}

			// the debug thread is gone: carry on with the failure
			return false;
		}

		HRESULT await_resume() const {
			auto result = Data->Result;
			delete Data;
			return result;
		}
	};

	Task<HRESULT> Run(std::function<HRESULT()> action) {
		co_return co_await EngineAwaiter{ this, std::move(action) };
	}

	// runs the function on the debug thread, after whatever is queued or running there
	bool Queue(std::function<void()> func);

	static LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	HRESULT CreateMessageWindow();
	void OnNotify();
	void OnResume(AsyncOpData* data);
	void SetTargetType(TargetType type);

	DWORD DebuggerThread();
	HRESULT CreateInternal();
	void UpdateStatus();
	bool DoPostCommand();
	HRESULT EnumThreads(const ProcessInfo& pi, std::vector<ThreadInfo>& threads, ULONG currentThread);
	void PostContextChanged();
	void PostScopeChanged();
	// flattens the group into rows, expanding the entries whose path is in 'expanded' (debug thread)
	HRESULT WalkSymbolGroup(IDebugSymbolGroup2* group, const std::set<std::wstring>& expanded, std::vector<SymbolEntry>& entries);

	void PostNotification(DebuggerNotificationType type, void* data);

private:
	NotificationHandler m_Handler;
	HWND m_hWnd{};
	wil::unique_handle m_hDebugThread;
	wil::unique_handle m_hShutdownEvent;
	wil::unique_handle m_hInitEvent;
	wil::unique_handle m_hDoneEvent;
	HRESULT m_InitResult{ E_FAIL };

	// engine callbacks (debug thread) waiting to be delivered (message window thread)
	std::mutex m_QueueLock;
	std::vector<Notification> m_Queue;
	bool m_WakePosted{ false };

	std::atomic<ULONG> m_Status{ DEBUG_STATUS_NO_DEBUGGEE };
	// the message window thread only
	int m_PendingOps{ 0 };
	TargetType m_TargetType{ TargetType::Unknown };
	// the debug thread only
	bool m_StateChanged{ false };
	bool m_BreakpointHit{ false };
	// while the engine's current thread is switched by the debugger itself (looking at the threads), nobody is told
	int m_InternalSwitch{ 0 };

	CComPtr<IDebugClient7> m_spClient;
	CComPtr<IDebugAdvanced4> m_spAdvanced;
	CComPtr<IDebugDataSpaces4> m_spData;
	CComPtr<IDebugRegisters2> m_spRegisters;
	CComPtr<IDebugSystemObjects4> m_spSystem;
	CComPtr<IDebugSymbols5> m_spSymbols;
	CComPtr<IDebugControl7> m_spControl;
	CComPtr<IDebugHost> m_spDebugHost;
	CComPtr<IDataModelManager> m_spDataMgr;

	// Inherited via IDebugEventCallbacks (all of these run on the debug thread)

	virtual HRESULT __stdcall GetInterestMask(PULONG Mask) override {
		*Mask = DEBUG_EVENT_BREAKPOINT | DEBUG_EVENT_CHANGE_DEBUGGEE_STATE | DEBUG_EVENT_CREATE_PROCESS | DEBUG_EVENT_CREATE_THREAD
			| DEBUG_EVENT_EXCEPTION | DEBUG_EVENT_LOAD_MODULE | DEBUG_EVENT_SYSTEM_ERROR | DEBUG_EVENT_CHANGE_ENGINE_STATE | DEBUG_EVENT_UNLOAD_MODULE
			| DEBUG_EVENT_EXIT_PROCESS | DEBUG_EVENT_EXIT_THREAD | DEBUG_EVENT_SESSION_STATUS | DEBUG_EVENT_CHANGE_SYMBOL_STATE;

		return S_OK;
	}
	virtual HRESULT __stdcall Breakpoint(PDEBUG_BREAKPOINT Bp) override {
		m_BreakpointHit = true;
		m_StateChanged = true;

		auto data = new BreakpointData;
		data->Breakpoint = Bp;
		PostNotification(DebuggerNotificationType::Breakpoint, data);

		return DEBUG_STATUS_BREAK;
	}
	virtual HRESULT __stdcall Exception(PEXCEPTION_RECORD64 Exception, ULONG FirstChance) override {
		m_BreakpointHit = Exception->ExceptionCode == 0x80000004 || Exception->ExceptionCode == 0x80000003 || FirstChance == 0;
		m_StateChanged = true;

		return m_BreakpointHit ? DEBUG_STATUS_BREAK : DEBUG_STATUS_NO_CHANGE;
	}

	virtual HRESULT __stdcall CreateThread(ULONG64 Handle, ULONG64 DataOffset, ULONG64 StartOffset) override {
		auto data = new ThreadCreateData;
		data->Handle = (HANDLE)Handle;
		data->Teb = DataOffset;
		data->StartAddress = StartOffset;
		m_spSystem->GetCurrentThreadId(&data->Id);
		m_spSystem->GetCurrentThreadSystemId(&data->ThreadId);
		m_spSystem->GetCurrentProcessSystemId(&data->Pid);

		PostNotification(DebuggerNotificationType::ThreadCreated, data);

		return DEBUG_STATUS_NO_CHANGE;
	}
	virtual HRESULT __stdcall ExitThread(ULONG ExitCode) override {
		auto data = new ExitThreadData;
		data->ExitCode = ExitCode;
		m_spSystem->GetCurrentThreadId(&data->Id);
		m_spSystem->GetCurrentThreadSystemId(&data->ThreadId);
		PostNotification(DebuggerNotificationType::ExitThread, data);

		return DEBUG_STATUS_NO_CHANGE;
	}
	virtual HRESULT __stdcall ExitProcess(ULONG ExitCode) override {
		auto data = new ExitThreadData;
		data->ExitCode = ExitCode;
		PostNotification(DebuggerNotificationType::ExitProcess, data);

		return DEBUG_STATUS_BREAK;
	}
	virtual HRESULT __stdcall LoadModule(ULONG64 ImageFileHandle, ULONG64 BaseOffset, ULONG ModuleSize, PCSTR ModuleName, PCSTR ImageName, ULONG CheckSum, ULONG TimeDateStamp) override {
		PostNotification(DebuggerNotificationType::ModuleLoaded, new ModuleEventData{ BaseOffset, std::wstring(CA2W(ImageName ? ImageName : (ModuleName ? ModuleName : ""))) });
		return DEBUG_STATUS_NO_CHANGE;
	}
	virtual HRESULT __stdcall UnloadModule(PCSTR ImageBaseName, ULONG64 BaseOffset) override {
		PostNotification(DebuggerNotificationType::ModuleUnloaded, new ModuleEventData{ BaseOffset, std::wstring(CA2W(ImageBaseName ? ImageBaseName : "")) });
		return DEBUG_STATUS_NO_CHANGE;
	}
	virtual HRESULT __stdcall SystemError(ULONG Error, ULONG Level) override {
		return S_OK;
	}
	virtual HRESULT __stdcall SessionStatus(ULONG Status) override {
		UpdateStatus();

		return S_OK;
	}
	virtual HRESULT __stdcall ChangeDebuggeeState(ULONG Flags, ULONG64 Argument) override {
		m_StateChanged = true;
		UpdateStatus();
		// registers written to (the engine says everything has changed when the target stops: that is not this)
		if ((Flags & DEBUG_CDS_REGISTERS) && !(Flags & DEBUG_CDS_DATA))
			PostContextChanged();
		return S_OK;
	}
	virtual HRESULT __stdcall ChangeEngineState(ULONG Flags, ULONG64 Argument) override {
		m_StateChanged = true;
		UpdateStatus();
		if (Flags & DEBUG_CES_BREAKPOINTS)
			PostNotification(DebuggerNotificationType::BreakpointsChanged, nullptr);
		if ((Flags & DEBUG_CES_CURRENT_THREAD) && m_InternalSwitch == 0)
			PostContextChanged();
		return S_OK;
	}
	virtual HRESULT __stdcall ChangeSymbolState(ULONG Flags, ULONG64 Argument) override {
		// (.frame, the call stack's selection, or the engine going back to frame 0; not the debugger's own look at other threads)
		if ((Flags & DEBUG_CSS_SCOPE) && m_InternalSwitch == 0)
			PostScopeChanged();
		return S_OK;
	}
	virtual HRESULT __stdcall CreateProcessW(ULONG64 ImageFileHandle, ULONG64 Handle, ULONG64 BaseOffset, ULONG ModuleSize, PCSTR ModuleName, PCSTR ImageName,
		ULONG CheckSum, ULONG TimeDateStamp, ULONG64 InitialThreadHandle, ULONG64 ThreadDataOffset, ULONG64 StartOffset) override {
		auto data = new CreateProcessData;
		data->BaseAddress = reinterpret_cast<void*>(BaseOffset);
		data->hFile = reinterpret_cast<HANDLE>(ImageFileHandle);
		data->hProcess = reinterpret_cast<HANDLE>(Handle);
		data->ImageName = ImageName;
		data->ModuleSize = ModuleSize;
		data->DateTimeStamp = TimeDateStamp;
		data->Checksum = CheckSum;
		data->hThread = reinterpret_cast<HANDLE>(InitialThreadHandle);
		data->ThreadDataOffset = reinterpret_cast<void*>(ThreadDataOffset);
		PostNotification(DebuggerNotificationType::ProcessCreated, data);

		return DEBUG_STATUS_NO_CHANGE;
	}

	// Inherited via IDebugOutputCallbacksWide
	virtual HRESULT __stdcall Output(ULONG Mask, PCWSTR Text) override {
		ATLASSERT(false);
		return E_NOTIMPL;
	}

	virtual HRESULT __stdcall Output(ULONG Mask, PCSTR Text) override {
		ATLASSERT(false);
		return E_NOTIMPL;
	}

	// Inherited via IDebugOutputCallbacks2
	HRESULT __stdcall Output2(ULONG Which, ULONG Flags, ULONG64 Arg, PCWSTR Text) override {
		auto data = new OutputData;
		data->IsDml = Which == DEBUG_OUTCB_DML;
		if (Text)
			data->Text = Text;
		data->Mask = static_cast<OutputDataMask>(Arg);
		PostNotification(DebuggerNotificationType::Output, data);
		return S_OK;
	}
};

DebuggerX::Impl::Impl() {
	ATLASSERT(g_Debugger == nullptr);
	g_Debugger = this;
}

DebuggerX::Impl::~Impl() {
	g_Debugger = nullptr;

	// callbacks that were never delivered
	for (auto& n : m_Queue)
		FreeNotification(n.Type, n.Data);
}

HRESULT DebuggerX::Impl::Create(NotificationHandler handler) {
	if (m_hWnd)
		return E_UNEXPECTED;

	m_Handler = std::move(handler);
	HR(CreateMessageWindow());

	m_hShutdownEvent.reset(::CreateEvent(nullptr, TRUE, FALSE, nullptr));
	m_hInitEvent.reset(::CreateEvent(nullptr, TRUE, FALSE, nullptr));
	m_hDoneEvent.reset(::CreateEvent(nullptr, TRUE, FALSE, nullptr));
	if (!m_hShutdownEvent || !m_hInitEvent || !m_hDoneEvent)
		return HRESULT_FROM_WIN32(::GetLastError());

	m_hDebugThread.reset(::CreateThread(nullptr, 0,
		[](LPVOID p) -> DWORD { return static_cast<Impl*>(p)->DebuggerThread(); },
		this, 0, nullptr));
	if (!m_hDebugThread)
		return HRESULT_FROM_WIN32(::GetLastError());

	// the engine is created on the debug thread; the thread never waits for this one, so blocking here is safe
	::WaitForSingleObject(m_hInitEvent.get(), INFINITE);
	return m_InitResult;
}

bool DebuggerX::Impl::Shutdown() {
	if (m_hDebugThread) {
		// A running target keeps the debug thread inside WaitForEvent, out of reach of the request below.
		if (m_spControl && m_Status != DEBUG_STATUS_NO_DEBUGGEE)
			Break();

		const auto live = (m_TargetType & TargetType::Live) == TargetType::Live;
		if (Queue([this, live] {
			// the data model belongs to the client: let go of it first
			m_spDebugHost.Release();
			m_spDataMgr.Release();
			if (m_spClient) {
				if (live)
					m_spClient->DetachProcesses();
				m_spClient->EndSession(DEBUG_END_ACTIVE_TERMINATE);
			}
			m_spClient.Release();
			m_spAdvanced.Release();
			m_spSymbols.Release();
			m_spControl.Release();
			m_spData.Release();
			m_spRegisters.Release();
			m_spSystem.Release();
			::SetEvent(m_hDoneEvent.get());
			})) {
			if (::WaitForSingleObject(m_hDoneEvent.get(), 5000) != WAIT_OBJECT_0)
				return false;
		}

		::SetEvent(m_hShutdownEvent.get());
		if (::WaitForSingleObject(m_hDebugThread.get(), 5000) != WAIT_OBJECT_0)
			return false;
		m_hDebugThread.reset();
	}

	if (m_hWnd) {
		::DestroyWindow(m_hWnd);
		m_hWnd = nullptr;
	}
	return true;
}

bool DebuggerX::Impl::Queue(std::function<void()> func) {
	auto p = new std::function<void()>(std::move(func));
	if (::QueueUserAPC([](ULONG_PTR arg) {
		std::unique_ptr<std::function<void()>> f(reinterpret_cast<std::function<void()>*>(arg));
		(*f)();
		}, m_hDebugThread.get(), reinterpret_cast<ULONG_PTR>(p)))
		return true;

	delete p;
	return false;
}

LRESULT CALLBACK DebuggerX::Impl::WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_NCCREATE:
			::SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCT*>(lParam)->lpCreateParams));
			break;

		case WM_DBG_NOTIFY:
			reinterpret_cast<Impl*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA))->OnNotify();
			return 0;

		case WM_DBG_RESUME:
			reinterpret_cast<Impl*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA))->OnResume(reinterpret_cast<AsyncOpData*>(lParam));
			return 0;
	}
	return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

HRESULT DebuggerX::Impl::CreateMessageWindow() {
	auto hInstance = ::GetModuleHandle(nullptr);
	WNDCLASSEXW wc{ sizeof(wc) };
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = WindowClassName;
	if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
		return HRESULT_FROM_WIN32(::GetLastError());

	m_hWnd = ::CreateWindowExW(0, WindowClassName, nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, this);
	return m_hWnd ? S_OK : HRESULT_FROM_WIN32(::GetLastError());
}

void DebuggerX::Impl::PostNotification(DebuggerNotificationType type, void* data) {
	{
		std::lock_guard locker(m_QueueLock);
		m_Queue.push_back({ type, data });
		// a wake-up that has not been handled yet will pick this one up as well
		if (m_WakePosted)
			return;
		m_WakePosted = true;
	}
	if (!::PostMessageW(m_hWnd, WM_DBG_NOTIFY, 0, 0)) {
		// the notification stays queued; the next one tries again
		std::lock_guard locker(m_QueueLock);
		m_WakePosted = false;
	}
}

void DebuggerX::Impl::OnNotify() {
	std::vector<Notification> batch;
	{
		std::lock_guard locker(m_QueueLock);
		batch.swap(m_Queue);
		m_WakePosted = false;
	}
	for (auto& n : batch) {
		// the session is over: there is no target any more (whatever the operation that started it will say)
		if (n.Type == DebuggerNotificationType::StatusChanged && *static_cast<ULONG*>(n.Data) == DEBUG_STATUS_NO_DEBUGGEE)
			SetTargetType(TargetType::Unknown);
		if (m_Handler)
			m_Handler(n.Type, n.Data);
		FreeNotification(n.Type, n.Data);
	}
}

void DebuggerX::Impl::SetTargetType(TargetType type) {
	if (type != m_TargetType) {
		m_TargetType = type;
		PostNotification(DebuggerNotificationType::TargetChanged, nullptr);
	}
}

void DebuggerX::Impl::OnResume(AsyncOpData* data) {
	if (--m_PendingOps == 0)
		PostNotification(DebuggerNotificationType::BusyChanged, nullptr);
	data->Handle.resume();
}

DWORD DebuggerX::Impl::DebuggerThread() {
	m_InitResult = CreateInternal();
	::SetEvent(m_hInitEvent.get());

	// queued calls run as APCs while the thread waits here
	while (::WaitForSingleObjectEx(m_hShutdownEvent.get(), INFINITE, TRUE) != WAIT_OBJECT_0)
		;

	return 0;
}

HRESULT DebuggerX::Impl::CreateInternal() {
	HR(::DebugCreate(__uuidof(IDebugClient7), reinterpret_cast<void**>(&m_spClient)));
	HR(m_spClient.QueryInterface(&m_spAdvanced));
	HR(m_spClient.QueryInterface(&m_spData));
	HR(m_spClient.QueryInterface(&m_spRegisters));
	HR(m_spClient.QueryInterface(&m_spSymbols));
	HR(m_spClient.QueryInterface(&m_spSystem));
	HR(m_spClient.QueryInterface(&m_spControl));
	CComQIPtr<IHostDataModelAccess> spDMAccess(m_spClient);
	if (spDMAccess) {
		spDMAccess->GetDataModel(&m_spDataMgr, &m_spDebugHost);
	}
	HR(m_spClient->SetEventCallbacks(this));
	HR(m_spClient->SetOutputCallbacksWide(this));
	m_spControl->AddEngineOptions(DEBUG_ENGOPT_PREFER_DML);
	return S_OK;
}

void DebuggerX::Impl::UpdateStatus() {
	ULONG status;
	if (FAILED(m_spControl->GetExecutionStatus(&status)))
		return;

	if (m_Status.exchange(status) != status) {
		// (a stop is often in another thread: who is current is known before anyone is told that the target has stopped)
		if (status == DEBUG_STATUS_BREAK)
			PostContextChanged();
		PostNotification(DebuggerNotificationType::StatusChanged, new ULONG(status));
	}
}

void DebuggerX::Impl::PostContextChanged() {
	ULONG id = 0;
	if (m_spSystem && SUCCEEDED(m_spSystem->GetCurrentThreadId(&id)))
		PostNotification(DebuggerNotificationType::ContextChanged, new ContextData{ id });
}

void DebuggerX::Impl::PostScopeChanged() {
	ULONG frame = 0;
	if (m_spSymbols && SUCCEEDED(m_spSymbols->GetCurrentScopeFrameIndex(&frame)))
		PostNotification(DebuggerNotificationType::ScopeChanged, new ScopeData{ frame });
}

bool DebuggerX::Impl::DoPostCommand() {
	UpdateStatus();
	m_spControl->OutputPromptWide(DEBUG_OUTCTL_THIS_CLIENT, nullptr);

	if (m_Status == DEBUG_STATUS_NO_DEBUGGEE) {
		m_spClient->EndSession(DEBUG_END_ACTIVE_TERMINATE);
		return false;
	}

	if (IsExecuting(m_Status)) {
		auto hr = m_spControl->WaitForEvent(DEBUG_WAIT_DEFAULT, INFINITE);
		UpdateStatus();
		if (FAILED(hr)) {
			if (m_Status == DEBUG_STATUS_NO_DEBUGGEE)
				return false;
		}
		if (m_Status == DEBUG_STATUS_BREAK)
			m_spControl->OutputCurrentState(DEBUG_OUTCTL_THIS_CLIENT, DEBUG_CURRENT_DEFAULT);
		m_StateChanged = false;
	}
	if (m_StateChanged) {
		if (m_Status == DEBUG_STATUS_NO_DEBUGGEE)
			return false;
		if (m_BreakpointHit) {
			m_BreakpointHit = false;
		}
		m_spControl->OutputPromptWide(DEBUG_OUTCTL_THIS_CLIENT, nullptr);
	}

	return true;
}

Task<HRESULT> DebuggerX::Impl::OpenDumpFile(PCWSTR filename) {
	// (the coroutine's frame outlives the call on the debug thread)
	ULONG debuggeeClass = DEBUG_CLASS_UNINITIALIZED;
	auto hr = co_await Run([this, filename = std::wstring(filename), &debuggeeClass] {
		HR(m_spClient->OpenDumpFileWide(filename.c_str(), 0));
		HR(m_spControl->WaitForEvent(0, INFINITE));
		ULONG qualifier;
		HR(m_spControl->GetDebuggeeType(&debuggeeClass, &qualifier));
		return S_OK;
		});
	if (SUCCEEDED(hr))
		SetTargetType(debuggeeClass == DEBUG_CLASS_KERNEL ? TargetType::DumpKernel : TargetType::DumpUser);
	co_return hr;
}

Task<HRESULT> DebuggerX::Impl::AttachToProcess(DWORD pid, AttachType attach) {
	auto hr = co_await Run([this, pid, attach] {
		m_spControl->AddEngineOptions(DEBUG_ENGOPT_INITIAL_BREAK);
		HR(m_spClient->AttachProcess(0, pid, static_cast<ULONG>(attach)));
		return m_spControl->WaitForEvent(0, INFINITE);
		});
	if (SUCCEEDED(hr))
		SetTargetType(TargetType::LiveUser);
	co_return hr;
}

Task<HRESULT> DebuggerX::Impl::LaunchAndAttach(PCWSTR exeName, PCWSTR commandLineArgs, PCWSTR workingDir, AttachType attach) {
	// the arguments are only good until the first suspension
	std::wstring cmd(L"\"");
	cmd += exeName;
	cmd += L"\"";
	if (commandLineArgs != nullptr && *commandLineArgs != L'\0')
		(cmd += L" ") += commandLineArgs;
	std::wstring initialDir(workingDir ? workingDir : L"");

	auto hr = co_await Run([this, cmd = std::move(cmd), initialDir = std::move(initialDir), attach]() mutable {
		DEBUG_CREATE_PROCESS_OPTIONS options = { 0 };
		options.CreateFlags = DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS;
		m_spControl->AddEngineOptions(DEBUG_ENGOPT_INITIAL_BREAK);
		HR(m_spClient->CreateProcessAndAttach2Wide(0, cmd.data(), &options, sizeof(options),
			initialDir.empty() ? nullptr : initialDir.c_str(), nullptr, 0, static_cast<ULONG>(attach)));
		HR(m_spControl->WaitForEvent(0, INFINITE));
		DoPostCommand();
		return S_OK;
		});
	if (SUCCEEDED(hr))
		SetTargetType(TargetType::LiveUser);
	co_return hr;
}

Task<HRESULT> DebuggerX::Impl::AttachLocalKernel() {
	auto hr = co_await Run([this] {
		HR(m_spClient->AttachKernel(DEBUG_ATTACH_LOCAL_KERNEL, nullptr));
		return m_spControl->WaitForEvent(0, INFINITE);
		});
	if (SUCCEEDED(hr))
		SetTargetType(TargetType::LocalKernel);
	co_return hr;
}

Task<HRESULT> DebuggerX::Impl::AttachKernel(PCWSTR connectOptions) {
	auto hr = co_await Run([this, options = std::wstring(connectOptions)] {
		m_spControl->AddEngineOptions(DEBUG_ENGOPT_INITIAL_BREAK);
		HR(m_spClient->AttachKernelWide(DEBUG_ATTACH_KERNEL_CONNECTION, options.c_str()));
		// waits for the target to connect; Break (SetInterrupt) ends the wait
		auto hr = m_spControl->WaitForEvent(0, INFINITE);
		UpdateStatus();
		if (FAILED(hr))
			m_spClient->EndSession(DEBUG_END_PASSIVE);
		return hr;
		});
	if (SUCCEEDED(hr))
		SetTargetType(TargetType::LiveKernel);
	co_return hr;
}

Task<HRESULT> DebuggerX::Impl::ExecuteCommand(PCWSTR command) {
	co_return co_await Run([this, cmd = std::wstring(command)] {
		HR(m_spControl->ExecuteWide(DEBUG_OUTCTL_ALL_CLIENTS, cmd.c_str(), DEBUG_EXECUTE_DEFAULT));
		DoPostCommand();
		return S_OK;
		});
}

Task<HRESULT> DebuggerX::Impl::OutputPrompt() {
	co_return co_await Run([this] {
		return m_spControl->OutputPrompt(DEBUG_OUTCTL_THIS_CLIENT, nullptr);
		});
}

Task<HRESULT> DebuggerX::Impl::Stop() {
	// a running target keeps the debug thread inside WaitForEvent: break in first, or the request waits for the target
	if (IsExecuting(m_Status))
		Break();

	co_return co_await Run([this] {
		return m_spClient->EndSession(DEBUG_END_ACTIVE_TERMINATE);
		});
}

Task<HRESULT> DebuggerX::Impl::Detach() {
	if (IsExecuting(m_Status))
		Break();

	const auto liveUser = (m_TargetType & TargetType::LiveUser) == TargetType::LiveUser;
	co_return co_await Run([this, liveUser] {
		// a user-mode process is let go of (and keeps running); anything else is just no longer looked at
		if (liveUser)
			HR(m_spClient->DetachProcesses());
		HR(m_spClient->EndSession(DEBUG_END_PASSIVE));
		UpdateStatus();
		return S_OK;
		});
}

Task<HRESULT> DebuggerX::Impl::SetDmlEnabled(bool enabled) {
	co_return co_await Run([this, enabled] {
		return enabled ? m_spControl->AddEngineOptions(DEBUG_ENGOPT_PREFER_DML) : m_spControl->RemoveEngineOptions(DEBUG_ENGOPT_PREFER_DML);
		});
}

HRESULT DebuggerX::Impl::Break() {
	// one of the few engine calls that may be made from any thread
	return m_spControl ? m_spControl->SetInterrupt(DEBUG_INTERRUPT_ACTIVE) : E_UNEXPECTED;
}

Task<HRESULT> DebuggerX::Impl::RunAsync(std::function<HRESULT(IDebugClient*)> func) {
	co_return co_await Run([this, func = std::move(func)] {
		CComQIPtr<IDebugClient> client(m_spClient);
		return client ? func(client) : E_NOINTERFACE;
		});
}

Task<HRESULT> DebuggerX::Impl::EnumModules(std::vector<ModuleInfo>& modules) {
	co_return co_await Run([this, &modules] {
		ULONG loaded, unloaded;
		HR(m_spSymbols->GetNumberModules(&loaded, &unloaded));
		WCHAR name[MAX_PATH];
		DEBUG_MODULE_PARAMETERS params;
		modules.reserve(loaded);
		for (ULONG i = 0; i < loaded; i++) {
			ModuleInfo mi;
			if (FAILED(m_spSymbols->GetModuleByIndex(i, &mi.BaseAddress)))
				continue;

			if (SUCCEEDED(m_spSymbols->GetModuleNameStringWide(DEBUG_MODNAME_IMAGE, i, 0, name, _countof(name), nullptr)))
				mi.ImageName = name;
			if (SUCCEEDED(m_spSymbols->GetModuleNameStringWide(DEBUG_MODNAME_MODULE, i, 0, name, _countof(name), nullptr)))
				mi.Name = name;
			if (SUCCEEDED(m_spSymbols->GetModuleNameStringWide(DEBUG_MODNAME_SYMBOL_FILE, i, 0, name, _countof(name), nullptr)))
				mi.SymbolFile = name;
			if (SUCCEEDED(m_spSymbols->GetModuleParameters(1, nullptr, i, &params))) {
				mi.Size = params.Size;
				mi.TimeStamp = params.TimeDateStamp;
				mi.Symbols = params.SymbolType;
			}
			modules.push_back(std::move(mi));
		}
		return S_OK;
		});
}

Task<HRESULT> DebuggerX::Impl::EnumProcesses(std::vector<ProcessInfo>& processes, std::vector<ThreadInfo>& threads) {
	if ((GetTargetType() & TargetType::Kernel) == TargetType::Kernel)
		co_return E_NOTIMPL;

	co_return co_await Run([this, &processes, &threads] {
		ULONG count;
		HR(m_spSystem->GetNumberProcesses(&count));
		processes.reserve(count);
		ULONG currentProcess, currentThread = DEBUG_ANY_ID;
		m_spSystem->GetCurrentProcessId(&currentProcess);
		m_spSystem->GetCurrentThreadId(&currentThread);
		// Switching threads puts the scope back to frame 0: the frame the user chose is restored afterwards (unannounced:
		// as far as anybody can tell, it never changed).
		struct Switching {
			Switching(Impl* self) : Self(self) {
				++Self->m_InternalSwitch;
				if (FAILED(Self->m_spSymbols->GetCurrentScopeFrameIndex(&Frame)))
					Frame = 0;
			}
			~Switching() {
				if (Frame)
					Self->m_spSymbols->SetScopeFrameByIndex(Frame);
				--Self->m_InternalSwitch;
			}
			Impl* Self;
			ULONG Frame{ 0 };
		} switching(this);
		WCHAR exe[MAX_PATH * 2];
		ULONG64 d;
		for (ULONG i = 0; i < count; i++) {
			ProcessInfo pi;
			if (FAILED(m_spSystem->GetProcessIdsByIndex(i, 1, &pi.Id, &pi.ProcessId)))
				continue;
			m_spSystem->SetCurrentProcessId(pi.Id);
			m_spSystem->GetCurrentProcessDataOffset(&pi.Peb);
			m_spSystem->GetCurrentProcessHandle(&d);
			pi.Handle = (HANDLE)d;
			m_spSystem->GetCurrentProcessUpTime(&pi.UpTimeSec);
			EnumThreads(pi, threads, pi.Id == currentProcess ? currentThread : DEBUG_ANY_ID);
			if (SUCCEEDED(m_spSystem->GetCurrentProcessExecutableNameWide(exe, _countof(exe), nullptr)))
				pi.ImagePath = exe;

			processes.push_back(pi);
		}
		m_spSystem->SetCurrentProcessId(currentProcess);
		m_spSystem->SetCurrentThreadId(currentThread);
		return S_OK;
		});
}

Task<HRESULT> DebuggerX::Impl::GetRegisters(std::vector<RegisterInfo>& registers) {
	co_return co_await Run([this, &registers] {
		ULONG count;
		HR(m_spRegisters->GetNumberRegisters(&count));
		std::vector<DEBUG_VALUE> values(count);
		// (as of the scope frame; all at once; one that cannot be read fails them all, then they are asked for one at a time)
		const bool all = SUCCEEDED(m_spRegisters->GetValues2(DEBUG_REGSRC_FRAME, count, nullptr, 0, values.data()));
		registers.reserve(count);
		WCHAR name[64];
		for (ULONG i = 0; i < count; i++) {
			if (!all && FAILED(m_spRegisters->GetValues2(DEBUG_REGSRC_FRAME, 1, nullptr, i, &values[i])))
				continue;
			if (FAILED(m_spRegisters->GetDescriptionWide(i, name, _countof(name), nullptr, nullptr)))
				continue;
			registers.push_back({ name, FormatValue(m_spControl, values[i]) });
		}
		return S_OK;
		});
}

Task<HRESULT> DebuggerX::Impl::GetStackTrace(std::vector<StackFrameInfo>& frames, ULONG maxFrames) {
	co_return co_await Run([this, &frames, maxFrames] {
		std::vector<DEBUG_STACK_FRAME> raw(maxFrames);
		ULONG filled = 0;
		HR(m_spControl->GetStackTrace(0, 0, 0, raw.data(), maxFrames, &filled));
		frames.reserve(filled);
		WCHAR name[512];
		for (ULONG i = 0; i < filled; i++) {
			auto& f = raw[i];
			StackFrameInfo fi{ f.FrameNumber, f.InstructionOffset, f.ReturnOffset, f.FrameOffset, f.StackOffset, {} };
			ULONG64 displacement = 0;
			if (SUCCEEDED(m_spSymbols->GetNameByOffsetWide(f.InstructionOffset, name, _countof(name), nullptr, &displacement)))
				fi.Symbol = displacement ? std::format(L"{}+0x{:X}", name, displacement) : std::wstring(name);
			frames.push_back(std::move(fi));
		}
		return S_OK;
		});
}

Task<HRESULT> DebuggerX::Impl::SetCurrentThread(ULONG threadId) {
	co_return co_await Run([this, threadId] {
		HR(m_spSystem->SetCurrentThreadId(threadId));
		// (the engine may or may not say so itself, when it is asked to switch by its own client)
		PostContextChanged();
		return S_OK;
		});
}

Task<HRESULT> DebuggerX::Impl::SetScopeFrame(ULONG frameIndex) {
	co_return co_await Run([this, frameIndex] {
		HR(m_spSymbols->SetScopeFrameByIndex(frameIndex));
		// (as with the thread: whether the engine says so itself is not to be relied on; the receiver ignores a repeat)
		PostScopeChanged();
		return S_OK;
		});
}

Task<HRESULT> DebuggerX::Impl::GetScope(ULONG64& instructionOffset, ULONG& frameIndex) {
	co_return co_await Run([this, &instructionOffset, &frameIndex] {
		HR(m_spSymbols->GetScope(&instructionOffset, nullptr, nullptr, 0));
		return m_spSymbols->GetCurrentScopeFrameIndex(&frameIndex);
		});
}

HRESULT DebuggerX::Impl::WalkSymbolGroup(IDebugSymbolGroup2* group, const std::set<std::wstring>& expanded, std::vector<SymbolEntry>& entries) {
	ULONG count;
	HR(group->GetNumberSymbols(&count));
	std::vector<std::wstring> parents;		// the names of the entry's ancestors, by depth
	WCHAR text[1024];
	// (expanding an entry inserts its children right after it: the count grows as the walk goes)
	for (ULONG i = 0; i < count; i++) {
		DEBUG_SYMBOL_PARAMETERS params;
		if (FAILED(group->GetSymbolParameters(i, 1, &params)))
			continue;

		SymbolEntry entry;
		entry.Depth = params.Flags & DEBUG_SYMBOL_EXPANSION_LEVEL_MASK;
		if (SUCCEEDED(group->GetSymbolNameWide(i, text, _countof(text), nullptr)))
			entry.Name = text;
		parents.resize(entry.Depth);
		for (auto& name : parents)
			(entry.Path += name) += L'\n';
		entry.Path += entry.Name;
		parents.push_back(entry.Name);

		entry.Expandable = params.SubElements > 0;
		entry.Expanded = (params.Flags & DEBUG_SYMBOL_EXPANDED) != 0;
		if (entry.Expandable && !entry.Expanded && expanded.contains(entry.Path) && SUCCEEDED(group->ExpandSymbol(i, TRUE))) {
			entry.Expanded = true;
			group->GetNumberSymbols(&count);
		}
		if (SUCCEEDED(group->GetSymbolTypeNameWide(i, text, _countof(text), nullptr)))
			entry.Type = text;
		if (SUCCEEDED(group->GetSymbolValueTextWide(i, text, _countof(text), nullptr)))
			entry.Value = text;
		entries.push_back(std::move(entry));
	}
	return S_OK;
}

Task<HRESULT> DebuggerX::Impl::GetLocals(std::vector<SymbolEntry>& entries, std::vector<std::wstring> expanded) {
	co_return co_await Run([this, &entries, expanded = std::set<std::wstring>(expanded.begin(), expanded.end())] {
		// (a new group each time: its values are read when it is made)
		CComPtr<IDebugSymbolGroup2> group;
		HR(m_spSymbols->GetScopeSymbolGroup2(DEBUG_SCOPE_GROUP_ALL, nullptr, &group));
		return WalkSymbolGroup(group, expanded, entries);
		});
}

Task<HRESULT> DebuggerX::Impl::EvaluateWatches(std::vector<SymbolEntry>& entries, std::vector<std::wstring> expressions, std::vector<std::wstring> expanded) {
	co_return co_await Run([this, &entries, expressions = std::move(expressions), expanded = std::set<std::wstring>(expanded.begin(), expanded.end())] {
		// a group for each expression: one that does not evaluate is not in a group, and has a row of its own all the same
		for (auto& expr : expressions) {
			CComPtr<IDebugSymbolGroup2> group;
			HR(m_spSymbols->CreateSymbolGroup2(&group));
			ULONG index = DEBUG_ANY_ID;
			// (a name it does not know is S_FALSE, not a failure: the entry is there, and its "value" is the engine's
			// banner about unqualified symbols)
			auto hr = group->AddSymbolWide(expr.c_str(), &index);
			if (hr != S_OK) {
				SymbolEntry entry;
				entry.Path = entry.Name = expr;
				entry.Value = SUCCEEDED(hr) ? std::wstring(L"<unknown symbol or invalid expression>")
					: std::format(L"<cannot evaluate: 0x{:08X}>", static_cast<unsigned>(hr));
				entry.Error = true;
				entries.push_back(std::move(entry));
				continue;
			}
			WalkSymbolGroup(group, expanded, entries);
		}
		return S_OK;
		});
}

Task<HRESULT> DebuggerX::Impl::EnumBreakpoints(std::vector<BreakpointInfo>& breakpoints) {
	co_return co_await Run([this, &breakpoints] {
		ULONG count;
		HR(m_spControl->GetNumberBreakpoints(&count));
		breakpoints.reserve(count);
		WCHAR text[1024];
		for (ULONG i = 0; i < count; i++) {
			CComPtr<IDebugBreakpoint2> bp;
			DEBUG_BREAKPOINT_PARAMETERS p;
			if (FAILED(m_spControl->GetBreakpointByIndex2(i, &bp)) || FAILED(bp->GetParameters(&p)))
				continue;

			BreakpointInfo bi{};
			bi.Id = p.Id;
			bi.Enabled = (p.Flags & DEBUG_BREAKPOINT_ENABLED) != 0;
			bi.Deferred = (p.Flags & DEBUG_BREAKPOINT_DEFERRED) != 0;
			bi.IsData = p.BreakType == DEBUG_BREAKPOINT_DATA;
			bi.Offset = p.Offset;
			bi.PassCount = p.PassCount;
			if (SUCCEEDED(bp->GetOffsetExpressionWide(text, _countof(text), nullptr)))
				bi.Expression = text;
			if (SUCCEEDED(bp->GetCommandWide(text, _countof(text), nullptr)))
				bi.Command = text;
			breakpoints.push_back(std::move(bi));
		}
		return S_OK;
		});
}

Task<HRESULT> DebuggerX::Impl::AddBreakpoint(PCWSTR expression) {
	co_return co_await Run([this, expression = std::wstring(expression)] {
		CComPtr<IDebugBreakpoint2> bp;
		HR(m_spControl->AddBreakpoint2(DEBUG_BREAKPOINT_CODE, DEBUG_ANY_ID, &bp));
		auto hr = bp->SetOffsetExpressionWide(expression.c_str());
		if (SUCCEEDED(hr))
			hr = bp->AddFlags(DEBUG_BREAKPOINT_ENABLED);
		if (FAILED(hr))
			m_spControl->RemoveBreakpoint2(bp.Detach());	// (the engine frees the breakpoint: nothing is left to Release)
		return hr;
		});
}

Task<HRESULT> DebuggerX::Impl::RemoveBreakpoint(ULONG id) {
	co_return co_await Run([this, id] {
		CComPtr<IDebugBreakpoint2> bp;
		HR(m_spControl->GetBreakpointById2(id, &bp));
		return m_spControl->RemoveBreakpoint2(bp.Detach());	// (the engine frees the breakpoint: nothing is left to Release)
		});
}

Task<HRESULT> DebuggerX::Impl::SetBreakpointEnabled(ULONG id, bool enabled) {
	co_return co_await Run([this, id, enabled] {
		CComPtr<IDebugBreakpoint2> bp;
		HR(m_spControl->GetBreakpointById2(id, &bp));
		return enabled ? bp->AddFlags(DEBUG_BREAKPOINT_ENABLED) : bp->RemoveFlags(DEBUG_BREAKPOINT_ENABLED);
		});
}

Task<HRESULT> DebuggerX::Impl::GetInstructionOffset(ULONG64& offset) {
	co_return co_await Run([this, &offset] {
		return m_spRegisters->GetInstructionOffset(&offset);
		});
}

Task<HRESULT> DebuggerX::Impl::Disassemble(ULONG64 address, ULONG count, std::vector<DisassemblyLine>& lines) {
	co_return co_await Run([this, address, count, &lines] {
		lines.reserve(count);
		WCHAR buffer[256];
		auto offset = address;
		for (ULONG i = 0; i < count; i++) {
			ULONG64 end;
			if (FAILED(m_spControl->DisassembleWide(offset, 0, buffer, _countof(buffer), nullptr, &end)))
				break;
			std::wstring text(buffer);
			while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r'))
				text.pop_back();
			lines.push_back({ offset, std::move(text) });
			if (end <= offset)		// no forward progress (e.g. unreadable memory): stop rather than loop
				break;
			offset = end;
		}
		return S_OK;
		});
}

Task<HRESULT> DebuggerX::Impl::Evaluate(PCWSTR expression, ULONG64& value) {
	co_return co_await Run([this, expression = std::wstring(expression), &value] {
		DEBUG_VALUE dv{};
		HR(m_spControl->EvaluateWide(expression.c_str(), DEBUG_VALUE_INT64, &dv, nullptr));
		value = dv.I64;
		return S_OK;
		});
}

Task<HRESULT> DebuggerX::Impl::ReadMemory(ULONG64 address, std::vector<BYTE>& buffer, ULONG size) {
	co_return co_await Run([this, address, &buffer, size] {
		buffer.resize(size);
		ULONG read = 0;
		HR(m_spData->ReadVirtual(address, buffer.data(), size, &read));
		buffer.resize(read);
		return S_OK;
		});
}

HRESULT DebuggerX::Impl::EnumThreads(const ProcessInfo& pi, std::vector<ThreadInfo>& threads, ULONG currentThread) {
	ULONG count;
	HR(m_spSystem->GetNumberThreads(&count));
	threads.reserve(count);
	ULONG restore;
	m_spSystem->GetCurrentThreadId(&restore);
	ULONG64 d;
	for (ULONG i = 0; i < count; i++) {
		ThreadInfo ti;
		if (FAILED(m_spSystem->GetThreadIdsByIndex(i, 1, &ti.Id, &ti.ThreadId)))
			continue;
		m_spSystem->SetCurrentThreadId(ti.Id);
		m_spSystem->GetCurrentThreadDataOffset(&ti.Teb);
		m_spSystem->GetCurrentThreadHandle(&d);
		ti.Handle = (HANDLE)d;
		ti.Pid = pi.ProcessId;
		ti.Current = ti.Id == currentThread;
		threads.push_back(ti);
	}
	m_spSystem->SetCurrentThreadId(restore);
	return S_OK;
}

DebuggerX::DebuggerX() {
	// The engine holds references to the object (as its callbacks) and lets go of them on the debug thread, when the
	// client is released: the last one would delete the object under our feet. Hold a reference of our own.
	auto impl = new CComObject<Impl>;
	impl->AddRef();
	m_Impl = impl;
}

DebuggerX::~DebuggerX() {
	// if the engine does not answer, the debug thread still refers to the object: leave it be
	if (m_Impl->Shutdown())
		static_cast<CComObject<Impl>*>(m_Impl)->Release();
}

HRESULT DebuggerX::Create(NotificationHandler handler) {
	return m_Impl->Create(std::move(handler));
}

Task<HRESULT> DebuggerX::OpenDumpFile(PCWSTR filename) {
	return m_Impl->OpenDumpFile(filename);
}

Task<HRESULT> DebuggerX::AttachToProcess(DWORD pid, AttachType attach) {
	return m_Impl->AttachToProcess(pid, attach);
}

Task<HRESULT> DebuggerX::LaunchAndAttach(PCWSTR exeName, PCWSTR commandLineArgs, PCWSTR workingDir, AttachType attach) {
	return m_Impl->LaunchAndAttach(exeName, commandLineArgs, workingDir, attach);
}

Task<HRESULT> DebuggerX::AttachLocalKernel() {
	return m_Impl->AttachLocalKernel();
}

Task<HRESULT> DebuggerX::ExecuteCommand(PCWSTR command) {
	return m_Impl->ExecuteCommand(command);
}

Task<HRESULT> DebuggerX::OutputPrompt() {
	return m_Impl->OutputPrompt();
}

Task<HRESULT> DebuggerX::AttachKernel(PCWSTR connectOptions) {
	return m_Impl->AttachKernel(connectOptions);
}

Task<HRESULT> DebuggerX::Stop() {
	return m_Impl->Stop();
}

Task<HRESULT> DebuggerX::Detach() {
	return m_Impl->Detach();
}

Task<HRESULT> DebuggerX::SetDmlEnabled(bool enabled) {
	return m_Impl->SetDmlEnabled(enabled);
}

Task<HRESULT> DebuggerX::EnumModules(std::vector<ModuleInfo>& modules) {
	return m_Impl->EnumModules(modules);
}

Task<HRESULT> DebuggerX::EnumProcesses(std::vector<ProcessInfo>& processes, std::vector<ThreadInfo>& threads) {
	return m_Impl->EnumProcesses(processes, threads);
}

Task<HRESULT> DebuggerX::RunAsync(std::function<HRESULT(IDebugClient*)> func) {
	return m_Impl->RunAsync(std::move(func));
}

Task<HRESULT> DebuggerX::GetRegisters(std::vector<RegisterInfo>& registers) {
	return m_Impl->GetRegisters(registers);
}

Task<HRESULT> DebuggerX::GetStackTrace(std::vector<StackFrameInfo>& frames, ULONG maxFrames) {
	return m_Impl->GetStackTrace(frames, maxFrames);
}

Task<HRESULT> DebuggerX::SetCurrentThread(ULONG threadId) {
	return m_Impl->SetCurrentThread(threadId);
}

Task<HRESULT> DebuggerX::SetScopeFrame(ULONG frameIndex) {
	return m_Impl->SetScopeFrame(frameIndex);
}

Task<HRESULT> DebuggerX::GetScope(ULONG64& instructionOffset, ULONG& frameIndex) {
	return m_Impl->GetScope(instructionOffset, frameIndex);
}

Task<HRESULT> DebuggerX::GetLocals(std::vector<SymbolEntry>& entries, std::vector<std::wstring> expanded) {
	return m_Impl->GetLocals(entries, std::move(expanded));
}

Task<HRESULT> DebuggerX::EvaluateWatches(std::vector<SymbolEntry>& entries, std::vector<std::wstring> expressions, std::vector<std::wstring> expanded) {
	return m_Impl->EvaluateWatches(entries, std::move(expressions), std::move(expanded));
}

Task<HRESULT> DebuggerX::EnumBreakpoints(std::vector<BreakpointInfo>& breakpoints) {
	return m_Impl->EnumBreakpoints(breakpoints);
}

Task<HRESULT> DebuggerX::AddBreakpoint(PCWSTR expression) {
	return m_Impl->AddBreakpoint(expression);
}

Task<HRESULT> DebuggerX::RemoveBreakpoint(ULONG id) {
	return m_Impl->RemoveBreakpoint(id);
}

Task<HRESULT> DebuggerX::SetBreakpointEnabled(ULONG id, bool enabled) {
	return m_Impl->SetBreakpointEnabled(id, enabled);
}

Task<HRESULT> DebuggerX::GetInstructionOffset(ULONG64& offset) {
	return m_Impl->GetInstructionOffset(offset);
}

Task<HRESULT> DebuggerX::Disassemble(ULONG64 address, ULONG count, std::vector<DisassemblyLine>& lines) {
	return m_Impl->Disassemble(address, count, lines);
}

Task<HRESULT> DebuggerX::Evaluate(PCWSTR expression, ULONG64& value) {
	return m_Impl->Evaluate(expression, value);
}

Task<HRESULT> DebuggerX::ReadMemory(ULONG64 address, std::vector<BYTE>& buffer, ULONG size) {
	return m_Impl->ReadMemory(address, buffer, size);
}

HRESULT DebuggerX::Break() {
	return m_Impl->Break();
}

TargetType DebuggerX::GetTargetType() const {
	return m_Impl->GetTargetType();
}

bool DebuggerX::IsBusy() const {
	return m_Impl->IsBusy();
}

namespace Debugger::DataModel::ClientEx {
	IDataModelManager* GetManager() {
		return g_Debugger->GetDataManager();
	}
	IDebugHost* GetHost() {
		return g_Debugger->GetDebugHost();
	}
}
