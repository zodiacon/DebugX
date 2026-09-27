#include "pch.h"
#include "Debugger.h"
#include <atomic>
#include <functional>
#include <mutex>
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
			default: break;
		}
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
	Task<HRESULT> ExecuteCommand(PCWSTR command);
	Task<HRESULT> OutputPrompt();
	Task<HRESULT> Stop();
	Task<HRESULT> EnumModules(std::vector<ModuleInfo>& modules);
	Task<HRESULT> EnumProcesses(std::vector<ProcessInfo>& processes, std::vector<ThreadInfo>& threads);
	Task<HRESULT> RunAsync(std::function<HRESULT(IDebugClient*)> func);
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
	HRESULT EnumThreads(const ProcessInfo& pi, std::vector<ThreadInfo>& threads);

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
			| DEBUG_EVENT_EXIT_PROCESS | DEBUG_EVENT_EXIT_THREAD | DEBUG_EVENT_SESSION_STATUS;

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
		return DEBUG_STATUS_NO_CHANGE;
	}
	virtual HRESULT __stdcall UnloadModule(PCSTR ImageBaseName, ULONG64 BaseOffset) override {
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
		return S_OK;
	}
	virtual HRESULT __stdcall ChangeEngineState(ULONG Flags, ULONG64 Argument) override {
		m_StateChanged = true;
		UpdateStatus();
		return S_OK;
	}
	virtual HRESULT __stdcall ChangeSymbolState(ULONG Flags, ULONG64 Argument) override {
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

	if (m_Status.exchange(status) != status)
		PostNotification(DebuggerNotificationType::StatusChanged, new ULONG(status));
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
	auto hr = co_await Run([this, filename = std::wstring(filename)] {
		HR(m_spClient->OpenDumpFileWide(filename.c_str(), 0));
		HR(m_spControl->WaitForEvent(0, INFINITE));
		return S_OK;
		});
	if (SUCCEEDED(hr))
		SetTargetType(TargetType::Dump);
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
		ULONG currentProcess;
		m_spSystem->GetCurrentProcessId(&currentProcess);
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
			EnumThreads(pi, threads);
			if (SUCCEEDED(m_spSystem->GetCurrentProcessExecutableNameWide(exe, _countof(exe), nullptr)))
				pi.ImagePath = exe;

			processes.push_back(pi);
		}
		m_spSystem->SetCurrentProcessId(currentProcess);
		return S_OK;
		});
}

HRESULT DebuggerX::Impl::EnumThreads(const ProcessInfo& pi, std::vector<ThreadInfo>& threads) {
	ULONG count;
	HR(m_spSystem->GetNumberThreads(&count));
	threads.reserve(count);
	ULONG currentThread;
	m_spSystem->GetCurrentThreadId(&currentThread);
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
		threads.push_back(ti);
	}
	m_spSystem->SetCurrentThreadId(currentThread);
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

Task<HRESULT> DebuggerX::Stop() {
	return m_Impl->Stop();
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
