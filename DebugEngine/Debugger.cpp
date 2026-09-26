#include "pch.h"
#include "Debugger.h"
#include <functional>
#include <atlcom.h>
#include <atlstr.h>
#include <wil\resource.h>
#include <DbgModel.h>
//#include <DbgModelClientEx.h>

#pragma comment(lib, "dbgeng")

using namespace DebugEngine;

DebuggerX::Impl* g_Debugger;

#define HR(x) { auto _hr = (x); if(FAILED(_hr)) return _hr; }

namespace {
	constexpr UINT WM_ASYNC_ENGINE_RESUME = WM_APP + 0x500;

	// Carries a suspended coroutine's state across the hop from the debug
	// thread (where the queued engine call actually runs) back to the
	// thread that awaited it, via PostThreadMessage/WM_ASYNC_ENGINE_RESUME.
	// Ownership passes to EngineAwaiter::await_resume(), which deletes it.
	struct AsyncOpData {
		std::function<HRESULT()> Action;
		HRESULT Result{ E_FAIL };
		DWORD CallerThreadId{};
		std::coroutine_handle<> Handle;
	};
}

bool DebugEngine::PumpAsyncResume(const MSG& msg) {
	if (msg.message != WM_ASYNC_ENGINE_RESUME)
		return false;

	reinterpret_cast<AsyncOpData*>(msg.lParam)->Handle.resume();
	return true;
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

	ExecutionStatus GetExecutionStatusAsync() const {
		return static_cast<ExecutionStatus>(m_Status);
	}

	void FreeNotification(DebuggerNotificationType type, WPARAM id) {
		auto data = reinterpret_cast<void*>(id);
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

	HRESULT DetachAll() {
		return RunAsync([&]() {
			return m_spClient->DetachProcesses();
			});
	}

	HRESULT ExitDispatch() {
		return m_spClient->ExitDispatch(CComQIPtr<IDebugClient>(m_spClient));
	}

	void RunAsync(std::function<void(IDebugClient*)> func) {
		RunAsync([&](auto client) {
			func(client);
			return S_OK;
			}, CComQIPtr<IDebugClient>(m_spClient).p);

	}

	Task<HRESULT> RunAsyncCo(std::function<HRESULT(IDebugClient*)> func) {
		auto client = CComQIPtr<IDebugClient>(m_spClient).p;
		co_return co_await EngineAwaiter{ this, [func = std::move(func), client]() {
			return func(client);
			} };
	}

	bool DoPostCommand() {
		UpdateStatus();
		m_spControl->OutputPromptWide(DEBUG_OUTCTL_THIS_CLIENT, nullptr);

		if (m_Status == DEBUG_STATUS_NO_DEBUGGEE) {
			m_spClient->EndSession(DEBUG_END_ACTIVE_TERMINATE);
			return false;
		}

		if (m_Status == DEBUG_STATUS_GO || m_Status == DEBUG_STATUS_STEP_BRANCH || m_Status == DEBUG_STATUS_STEP_INTO || m_Status == DEBUG_STATUS_STEP_OVER) {
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

	void DispatchCallbacks(ULONG msec) {
		if (m_spClient)
			m_spClient->DispatchCallbacks(msec);
	}

	HRESULT Create(DWORD notifyThreadId, HWND hWndNotify);
	HRESULT AttachToProcess(DWORD pid, AttachType attach);
	HRESULT LaunchAndAttach(PCWSTR exeName, PCWSTR commandLineArgs, PCWSTR workingDir, AttachType attach);
	HRESULT AttachLocalKernel();
	HRESULT ExecuteCommand(PCSTR command);
	HRESULT ExecuteCommand(PCWSTR command);
	Task<HRESULT> ExecuteCommandAsync(PCWSTR command);
	HRESULT OpenDumpFile(PCWSTR filename);
	HRESULT Break();
	HRESULT Stop();
	HRESULT OutputPrompt();
	ExecutionStatus GetExecutionStatus();
	HRESULT EnumModules(std::vector<ModuleInfo>& modules);
	HRESULT EnumThreads(const ProcessInfo& pi, std::vector<ThreadInfo>& threads);
	HRESULT EnumProcesses(std::vector<ProcessInfo>& processes, std::vector<ThreadInfo>& threads);
	HRESULT Evaluate(PCSTR text, DEBUG_VALUE* value);
	bool IsLocalKernelDebuggerAvailable();
	bool IsBusy() const {
		return m_IsBusy;
	}

	TargetType GetTargetType() const {
		return m_TargetType;
	}

private:
	template<typename F>
	struct ApcData {
		ApcData(F& f) : Action(f) {}
		F& Action;
		HRESULT HResult;
		HANDLE hEvent;
	};

	template<typename F, typename T1>
	struct ApcData1 {
		ApcData1(F& f) : Action(f) {}
		F& Action;
		HRESULT HResult;
		HANDLE hEvent;
		T1 Arg1;
	};

	template<typename F, typename T1, typename T2>
	struct ApcData2 {
		ApcData2(F& f) : Action(f) {}
		F& Action;
		HRESULT HResult;
		HANDLE hEvent;
		T1 Arg1;
		T2 Arg2;
	};

	DWORD DebuggerThread();

	// Awaiter that queues an engine call to the debug thread (same as RunAsync)
	// but suspends the calling coroutine instead of blocking the calling
	// thread. Resumption happens via PumpAsyncResume() on the calling thread.
	struct EngineAwaiter {
		Impl* Self;
		std::function<HRESULT()> Action;
		AsyncOpData* Data{};

		bool await_ready() const noexcept { return false; }

		void await_suspend(std::coroutine_handle<> handle) {
			Data = new AsyncOpData{ std::move(Action), E_FAIL, ::GetCurrentThreadId(), handle };
			::QueueUserAPC([](ULONG_PTR p) {
				auto* d = reinterpret_cast<AsyncOpData*>(p);
				d->Result = d->Action();
				::PostThreadMessage(d->CallerThreadId, WM_ASYNC_ENGINE_RESUME, 0, reinterpret_cast<LPARAM>(d));
				}, Self->m_hDebugThread.get(), reinterpret_cast<ULONG_PTR>(Data));
		}

		HRESULT await_resume() const {
			auto result = Data->Result;
			delete Data;
			return result;
		}
	};

	template<typename F, typename T1>
	HRESULT RunAsync(F action, T1 arg) {
		ApcData1<F, T1> data(action);
		data.Arg1 = arg;
		data.hEvent = m_hAsyncEvent.get();

		::QueueUserAPC([](auto p) {
			auto d = (ApcData1<F, T1>*)p;
			d->HResult = (d->Action)(d->Arg1);
			::SetEvent(d->hEvent);
			}, m_hDebugThread.get(), (ULONG_PTR)&data);

		WaitWithMessages(m_hAsyncEvent.get());

		return data.HResult;
	}

	template<typename F, typename T1, typename T2>
	HRESULT RunAsync(F action, T1 arg1, T2 arg2) {
		ApcData2<F, T1, T2> data(action);
		data.Arg1 = arg1;
		data.Arg2 = arg2;
		data.hEvent = m_hAsyncEvent.get();

		::QueueUserAPC([](auto p) {
			auto d = (ApcData2<F, T1, T2>*)p;
			d->HResult = (d->Action)(d->Arg1, d->Arg2);
			::SetEvent(d->hEvent);
			}, m_hDebugThread.get(), (ULONG_PTR)&data);

		WaitWithMessages(m_hAsyncEvent.get());

		return data.HResult;
	}

	template<typename F>
	HRESULT RunAsync(F action) {
		ApcData<F> data(action);
		data.hEvent = m_hAsyncEvent.get();

		::QueueUserAPC([](DWORD_PTR p) {
			auto d = (ApcData<F>*)p;
			d->HResult = (d->Action());
			::SetEvent(d->hEvent);
			}, m_hDebugThread.get(), (ULONG_PTR)&data);

		WaitWithMessages(m_hAsyncEvent.get());

		return data.HResult;
	}

	HRESULT CreateInternal();
	HRESULT AttachToProcessInternal(DWORD pid, AttachType attach);
	HRESULT WaitForEventInternal(ULONG timeout);

private:
	void UpdateStatus();
	void WaitWithMessages(HANDLE hEvent);

	IMessageLoop* m_Ml;
	ULONG m_Status = DEBUG_STATUS_NO_CHANGE;
	wil::unique_handle m_hDebugThread;
	wil::unique_handle m_hShutdownEvent;
	wil::unique_handle m_hAsyncEvent;
	DWORD m_NotifyThreadId{ 0 };
	HWND m_NotifyHwnd{ nullptr };
	CComPtr<IDebugClient7> m_spClient;
	CComPtr<IDebugAdvanced4> m_spAdvanced;
	CComPtr<IDebugDataSpaces4> m_spData;
	CComPtr<IDebugRegisters2> m_spRegisters;
	CComPtr<IDebugSystemObjects4> m_spSystem;
	CComPtr<IDebugSymbols5> m_spSymbols;
	CComPtr<IDebugControl7> m_spControl;
	CComPtr<IDebugHost> m_spDebugHost;
	CComPtr<IDataModelManager> m_spDataMgr;

	TargetType m_TargetType = TargetType::Unknown;
	bool m_StateChanged : 1 { false};
	bool m_BreakpointHit : 1 { false };
	bool m_IsBusy : 1 { false};

	// Inherited via IDebugEventCallbacks

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
	virtual HRESULT CreateProcessW(ULONG64 ImageFileHandle, ULONG64 Handle, ULONG64 BaseOffset, ULONG ModuleSize, PCSTR ModuleName, PCSTR ImageName,
		ULONG CheckSum, ULONG TimeDateStamp, ULONG64 InitialThreadHandle, ULONG64 ThreadDataOffset, ULONG64 StartOffset) {
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

	void PostNotification(DebuggerNotificationType type, void* data) {
		if (m_NotifyThreadId)
			::PostThreadMessage(m_NotifyThreadId, static_cast<UINT>(type), reinterpret_cast<WPARAM>(data), reinterpret_cast<LPARAM>(data));
		else
			::PostMessage(m_NotifyHwnd, static_cast<UINT>(type), reinterpret_cast<WPARAM>(data), reinterpret_cast<LPARAM>(data));
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
	RunAsync([this]() {
		m_spClient->EndSession(DEBUG_END_ACTIVE_TERMINATE);
		m_spClient.Release();
		m_spAdvanced.Release();
		m_spSymbols.Release();
		m_spControl.Release();
		m_spData.Release();
		m_spRegisters.Release();
		m_spSystem.Release();
		return S_OK;
		});

	::SetEvent(m_hShutdownEvent.get());
	::WaitForSingleObject(m_hDebugThread.get(), 1000);
}

HRESULT DebuggerX::Impl::CreateInternal() {
	return RunAsync([this]() {
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
		});
}

HRESULT DebuggerX::Impl::AttachToProcessInternal(DWORD pid, AttachType attach) {
	auto hr = RunAsync([this](auto pid, auto attach) {
		m_spControl->AddEngineOptions(DEBUG_ENGOPT_INITIAL_BREAK);
		HR(m_spClient->AttachProcess(0, pid, static_cast<ULONG>(attach)));
		return m_spControl->WaitForEvent(0, INFINITE);
		}, pid, attach);
	if (SUCCEEDED(hr))
		m_TargetType = TargetType::LiveUser;
	return hr;
}

HRESULT DebuggerX::Impl::WaitForEventInternal(ULONG timeout) {
	return m_spControl->WaitForEvent(0, timeout);
}

void DebuggerX::Impl::UpdateStatus() {
	auto oldStatus = m_Status;
	m_spControl->GetExecutionStatus(&m_Status);
	if (m_Status != oldStatus) {
		auto data = new ULONG(m_Status);
		PostNotification(DebuggerNotificationType::StatusChanged, data);
	}
}

void DebuggerX::Impl::WaitWithMessages(HANDLE hEvent) {
	m_IsBusy = true;
	for (;;) {
		if (::WaitForSingleObject(hEvent, 0) == WAIT_OBJECT_0)
			break;

		DispatchCallbacks(10);
		MSG msg;
		while (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
	}
	m_IsBusy = false;
}

HRESULT DebuggerX::Impl::Create(DWORD notifyThreadId, HWND hNotifyWnd) {
	if (m_NotifyHwnd || m_NotifyThreadId)
		return E_UNEXPECTED;

	m_NotifyThreadId = notifyThreadId;
	m_NotifyHwnd = hNotifyWnd;
	m_hShutdownEvent.reset(::CreateEvent(nullptr, TRUE, FALSE, nullptr));
	ATLASSERT(m_hShutdownEvent);
	m_hAsyncEvent.reset(::CreateEvent(nullptr, FALSE, FALSE, nullptr));

	m_hDebugThread.reset(::CreateThread(nullptr, 0,
		[](auto p) { return ((Impl*)p)->DebuggerThread(); },
		this, 0, nullptr));
	ATLASSERT(m_hDebugThread);

	return CreateInternal();
}

HRESULT DebuggerX::Impl::AttachToProcess(DWORD pid, AttachType attach) {
	return AttachToProcessInternal(pid, attach);
}

HRESULT DebuggerX::Impl::LaunchAndAttach(PCWSTR exeName, PCWSTR commandLineArgs, PCWSTR workingDir, AttachType attach) {
	auto hr = RunAsync([=]() {
		CString cmd(L"\"");
		(cmd += exeName) += L"\"";
		if (commandLineArgs != nullptr && *commandLineArgs != L'\0')
			(cmd += L" ") += commandLineArgs;
		DEBUG_CREATE_PROCESS_OPTIONS options = { 0 };
		options.CreateFlags = DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS;
		m_spControl->AddEngineOptions(DEBUG_ENGOPT_INITIAL_BREAK);
		auto initialDir = workingDir;
		if (workingDir && *workingDir == L'\0')
			initialDir = nullptr;
		HR(m_spClient->CreateProcessAndAttach2Wide(0, cmd.GetBuffer(), &options, sizeof(options), initialDir, nullptr, 0, (ULONG)attach));
		HR(m_spControl->WaitForEvent(0, INFINITE));
		DoPostCommand();
		return S_OK;
		});
	if (SUCCEEDED(hr))
		m_TargetType = TargetType::LiveUser;
	return hr;
}

HRESULT DebuggerX::Impl::AttachLocalKernel() {
	auto hr = RunAsync([this]() {
		HR(m_spClient->AttachKernel(DEBUG_ATTACH_LOCAL_KERNEL, nullptr));
		return m_spControl->WaitForEvent(0, INFINITE);
		});
	if (SUCCEEDED(hr))
		m_TargetType = TargetType::LocalKernel;
	return hr;

}

HRESULT DebuggerX::Impl::ExecuteCommand(PCSTR command) {
	return RunAsync([this](auto cmd) {
		HR(m_spControl->Execute(DEBUG_OUTCTL_ALL_CLIENTS, cmd, DEBUG_EXECUTE_DEFAULT));
		DoPostCommand();
		return S_OK;
		}, command);
}

HRESULT DebuggerX::Impl::ExecuteCommand(PCWSTR command) {
	m_IsBusy = true;
	return RunAsync([this](auto cmd) {
		HR(m_spControl->ExecuteWide(DEBUG_OUTCTL_ALL_CLIENTS, cmd, DEBUG_EXECUTE_DEFAULT));
		DoPostCommand();
		m_IsBusy = false;
		return S_OK;
		}, command);
}

Task<HRESULT> DebuggerX::Impl::ExecuteCommandAsync(PCWSTR command) {
	m_IsBusy = true;
	// command is copied up front (before the first suspension point) since the
	// underlying engine call only runs later, on the debug thread.
	co_return co_await EngineAwaiter{ this, [this, cmd = std::wstring(command)]() {
		HR(m_spControl->ExecuteWide(DEBUG_OUTCTL_ALL_CLIENTS, cmd.c_str(), DEBUG_EXECUTE_DEFAULT));
		DoPostCommand();
		m_IsBusy = false;
		return S_OK;
		} };
}

HRESULT DebuggerX::Impl::OpenDumpFile(PCWSTR filename) {
	auto hr = RunAsync([&]() {
		HR(m_spClient->OpenDumpFileWide(filename, 0));
		HR(m_spControl->WaitForEvent(0, INFINITE));
		return S_OK;
		});
	if (SUCCEEDED(hr))
		m_TargetType = TargetType::Dump;
	return hr;
}

HRESULT DebuggerX::Impl::Break() {
	return m_spControl->SetInterrupt(DEBUG_INTERRUPT_ACTIVE);
}

HRESULT DebuggerX::Impl::Stop() {
	return RunAsync([this]() {
		m_Ml = nullptr;
		return m_spClient->EndSession(DEBUG_END_ACTIVE_TERMINATE);
		});
}

HRESULT DebuggerX::Impl::OutputPrompt() {
	return RunAsync([this]() {
		return m_spControl->OutputPrompt(DEBUG_OUTCTL_THIS_CLIENT, nullptr);
		});
}

ExecutionStatus DebuggerX::Impl::GetExecutionStatus() {
	ULONG status;
	RunAsync([this](ULONG* status) {
		return m_spControl->GetExecutionStatus(status);
		}, &status);

	return static_cast<ExecutionStatus>(status);
}

HRESULT DebuggerX::Impl::EnumModules(std::vector<ModuleInfo>& modules) {
	return RunAsync([this](auto& modules) {
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
		}, modules);
}

HRESULT DebuggerX::Impl::EnumProcesses(std::vector<ProcessInfo>& processes, std::vector<ThreadInfo>& threads) {
	if ((GetTargetType() & TargetType::Kernel) == TargetType::Kernel)
		return E_NOTIMPL;

	return RunAsync([&]() {
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

bool DebuggerX::Impl::IsLocalKernelDebuggerAvailable() {
	auto hr = RunAsync([this]() {
		return m_spClient->IsKernelDebuggerEnabled();
		});
	return hr == S_OK;
}

HRESULT DebuggerX::Impl::Evaluate(PCSTR text, DEBUG_VALUE* value) {
	return RunAsync([&](auto text, auto value) {
		return m_spControl->Evaluate(text, DEBUG_VALUE_INVALID, value, nullptr);
		}, text, value);
}

DWORD DebuggerX::Impl::DebuggerThread() {
	for (;;) {
		if (::WaitForSingleObjectEx(m_hShutdownEvent.get(), INFINITE, TRUE) == WAIT_OBJECT_0)
			break;
	}

	return 0;
}

DebuggerX::DebuggerX() : m_Impl(new CComObject<Impl>) {
}

DebuggerX::~DebuggerX() {
	delete m_Impl;
}

HRESULT DebuggerX::Create(DWORD notifyThreadId) {
	return m_Impl->Create(notifyThreadId, nullptr);
}

HRESULT DebuggerX::Create(HWND notifyHwnd) {
	return m_Impl->Create(0, notifyHwnd);
}

HRESULT DebuggerX::AttachToProcess(DWORD pid, AttachType attach) {
	return m_Impl->AttachToProcess(pid, attach);
}

HRESULT DebuggerX::LaunchAndAttach(PCWSTR exeName, PCWSTR commandLineArgs, PCWSTR workingDir, AttachType attach) {
	return m_Impl->LaunchAndAttach(exeName, commandLineArgs, workingDir, attach);
}

HRESULT DebuggerX::ExecuteCommand(PCSTR command) {
	return m_Impl->ExecuteCommand(command);
}

HRESULT DebuggerX::ExecuteCommand(PCWSTR command) {
	return m_Impl->ExecuteCommand(command);
}

Task<HRESULT> DebuggerX::ExecuteCommandAsync(PCWSTR command) {
	return m_Impl->ExecuteCommandAsync(command);
}

HRESULT DebuggerX::AttachLocalKernel() {
	return m_Impl->AttachLocalKernel();
}

HRESULT DebuggerX::Break() {
	return m_Impl->Break();
}

HRESULT DebugEngine::DebuggerX::Stop() {
	return m_Impl->Stop();
}

ExecutionStatus DebugEngine::DebuggerX::GetExecutionStatus() {
	return m_Impl->GetExecutionStatus();
}

HRESULT DebugEngine::DebuggerX::OutputPrompt() {
	return m_Impl->OutputPrompt();
}

HRESULT DebuggerX::EnumModules(std::vector<ModuleInfo>& modules) {
	return m_Impl->EnumModules(modules);
}

void DebuggerX::FreeNotification(DebuggerNotificationType type, WPARAM id) {
	return m_Impl->FreeNotification(type, id);
}

void DebuggerX::RunAsync(std::function<void(IDebugClient*)> func) {
	m_Impl->RunAsync(func);
}

Task<HRESULT> DebuggerX::RunAsyncCo(std::function<HRESULT(IDebugClient*)> func) {
	return m_Impl->RunAsyncCo(std::move(func));
}

ExecutionStatus DebuggerX::GetExecutionStatusAsync() const {
	return m_Impl->GetExecutionStatusAsync();
}

TargetType DebuggerX::GetTargetType() const {
	return m_Impl->GetTargetType();
}

HRESULT DebuggerX::OpenDumpFile(PCWSTR filename) {
	return m_Impl->OpenDumpFile(filename);
}


bool DebuggerX::IsLocalKernelDebuggerAvailable() {
	return m_Impl->IsLocalKernelDebuggerAvailable();
}

bool DebuggerX::IsBusy() const {
	return m_Impl->IsBusy();
}

HRESULT DebuggerX::DetachAll() {
	return m_Impl->DetachAll();
}

HRESULT DebuggerX::EnumProcesses(std::vector<ProcessInfo>& processes, std::vector<ThreadInfo>& threads) {
	return m_Impl->EnumProcesses(processes, threads);
}

void DebugEngine::DebuggerX::DispatchCallbacks(ULONG msec) {
	m_Impl->DispatchCallbacks(msec);
}

HRESULT DebuggerX::ExitDispatch() {
	return m_Impl->ExitDispatch();
}

namespace Debugger::DataModel::ClientEx {
	IDataModelManager* GetManager() {
		return g_Debugger->GetDataManager();
	}
	IDebugHost* GetHost() {
		return g_Debugger->GetDebugHost();
	}
}
