#include "pch.h"
#include "Debugger.h"
#include <functional>
#include <atlcom.h>

CComModule _Module;

#pragma comment(lib, "dbgeng")

using namespace DebugEngine;

#define HR(x) { auto _hr = (x); if(FAILED(_hr)) return _hr; }

struct Debugger::Impl : 
	CComObjectRoot, 
	IDebugEventCallbacks, IDebugOutputCallbacks {
	BEGIN_COM_MAP(Impl)
		COM_INTERFACE_ENTRY(IDebugEventCallbacks)
		COM_INTERFACE_ENTRY(IDebugOutputCallbacks)
	END_COM_MAP()

	Impl();
	~Impl();

	void FreeNotification(WPARAM id) {
		delete m_NotificationData[id];
	}

	HRESULT Create(DWORD notifyThreadId);
	HRESULT AttachToProcess(DWORD pid, AttachType attach);

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

	HRESULT WaitForEvent(UINT timeout = INFINITE);
	DWORD DebuggerThread();

	template<typename F, typename T1>
	HRESULT RunAsync(F action, T1 arg) {
		ApcData1<T1> data;
		data.Action = action;
		data.Arg1 = arg;
		data.hEvent = m_hAsyncEvent;

		::QueueUserAPC([](auto p) {
			auto d = (ApcData1<T1>*)p;
			d->HResult = d->Action(std::get<0>(d->Arg1));
			::SetEvent(m_hAsyncEvent);
			}, m_hDebugThread, (ULONG_PTR)&data);

		::WaitForSingleObject(data.hEvent, INFINITE);

		return data.HResult;
	}

	template<typename F, typename T1, typename T2>
	HRESULT RunAsync(F action, T1 arg1, T2 arg2) {
		ApcData2<F, T1, T2> data(action);
		data.Arg1 = arg1;
		data.Arg2 = arg2;
		data.hEvent = m_hAsyncEvent;

		::QueueUserAPC([](auto p) {
			auto d = (ApcData2<F, T1, T2>*)p;
			d->HResult = (d->Action)(d->Arg1, d->Arg2);
			::SetEvent(d->hEvent);
			}, m_hDebugThread, (ULONG_PTR)&data);

		::WaitForSingleObject(m_hAsyncEvent, INFINITE);

		return data.HResult;
	}

	template<typename F>
	HRESULT RunAsync(F action) {
		ApcData<F> data(action);
		data.hEvent = m_hAsyncEvent;

		::QueueUserAPC([](DWORD_PTR p) {
			auto d = (ApcData<F>*)p;
			d->HResult = (d->Action());
			::SetEvent(d->hEvent);
			}, m_hDebugThread, (ULONG_PTR)& data);

		::WaitForSingleObject(m_hAsyncEvent, INFINITE);

		return data.HResult;
	}

	HRESULT CreateInternal();
	HRESULT AttachToProcessInternal(DWORD pid, AttachType attach);
	HRESULT WaitForEventInternal(ULONG timeout);

private:
	static const int MaxNotifications = 64;

	HANDLE m_hDebugThread;
	HANDLE m_hShutdownEvent;
	HANDLE m_hAsyncEvent;
	DWORD m_NotifyThreadId;
	void* m_NotificationData[MaxNotifications];
	int m_CurrentNotification{ 0 };
	CComPtr<IDebugClient7> m_spClient;
	CComPtr<IDebugAdvanced4> m_spAdvanced;
	CComPtr<IDebugDataSpaces4> m_spData;
	CComPtr<IDebugRegisters2> m_spRegisters;
	CComPtr<IDebugSystemObjects4> m_spSystem;
	CComPtr<IDebugSymbols5> m_spSymbols;
	CComPtr<IDebugControl7> m_spControl;

	// Inherited via IDebugEventCallbacks

	virtual HRESULT __stdcall GetInterestMask(PULONG Mask) override {
		*Mask = DEBUG_EVENT_BREAKPOINT | DEBUG_EVENT_CHANGE_DEBUGGEE_STATE | DEBUG_EVENT_CREATE_PROCESS | DEBUG_EVENT_CREATE_THREAD
			| DEBUG_EVENT_EXCEPTION | DEBUG_EVENT_LOAD_MODULE | DEBUG_EVENT_SYSTEM_ERROR | DEBUG_EVENT_CHANGE_ENGINE_STATE | DEBUG_EVENT_UNLOAD_MODULE
			| DEBUG_EVENT_EXIT_PROCESS | DEBUG_EVENT_EXIT_THREAD | DEBUG_EVENT_SESSION_STATUS;

		return S_OK;
	}
	virtual HRESULT __stdcall Breakpoint(PDEBUG_BREAKPOINT Bp) override {
		return S_OK;
	}
	virtual HRESULT __stdcall Exception(PEXCEPTION_RECORD64 Exception, ULONG FirstChance) override {
		return S_OK;
	}
	virtual HRESULT __stdcall CreateThread(ULONG64 Handle, ULONG64 DataOffset, ULONG64 StartOffset) override {
		return S_OK;
	}
	virtual HRESULT __stdcall ExitThread(ULONG ExitCode) override {
		return S_OK;
	}
	virtual HRESULT __stdcall ExitProcess(ULONG ExitCode) override {
		return S_OK;
	}
	virtual HRESULT __stdcall LoadModule(ULONG64 ImageFileHandle, ULONG64 BaseOffset, ULONG ModuleSize, PCSTR ModuleName, PCSTR ImageName, ULONG CheckSum, ULONG TimeDateStamp) override {
		return S_OK;
	}
	virtual HRESULT __stdcall UnloadModule(PCSTR ImageBaseName, ULONG64 BaseOffset) override {
		return S_OK;
	}
	virtual HRESULT __stdcall SystemError(ULONG Error, ULONG Level) override {
		return S_OK;
	}
	virtual HRESULT __stdcall SessionStatus(ULONG Status) override {
		return S_OK;
	}
	virtual HRESULT __stdcall ChangeDebuggeeState(ULONG Flags, ULONG64 Argument) override {
		return S_OK;
	}
	virtual HRESULT __stdcall ChangeEngineState(ULONG Flags, ULONG64 Argument) override {
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

		return S_OK;
	}

	void PostNotification(DebuggerNotificationType type, void* data) {
		auto current = m_CurrentNotification;
		m_CurrentNotification = ++m_CurrentNotification % MaxNotifications;
		m_NotificationData[current] = data;
		::PostThreadMessage(m_NotifyThreadId, static_cast<UINT>(type), current, reinterpret_cast<LPARAM>(data));
	}

	// Inherited via IDebugOutputCallbacks
	virtual HRESULT __stdcall Output(ULONG Mask, PCSTR Text) override {
		auto data = new OutputData;
		data->Text = Text;
		data->Mask = static_cast<OutputDataMask>(Mask);
		PostNotification(DebuggerNotificationType::Output, data);
		return S_OK;
	}
};

Debugger::Impl::Impl() {
}

Debugger::Impl::~Impl() {
	::SetEvent(m_hShutdownEvent);
	::WaitForSingleObject(m_hDebugThread, 1000);
	::CloseHandle(m_hShutdownEvent);
	::CloseHandle(m_hDebugThread);
	::CloseHandle(m_hAsyncEvent);
}

HRESULT Debugger::Impl::CreateInternal() {
	return RunAsync([this]() {
		HR(::DebugCreate(__uuidof(IDebugClient7), reinterpret_cast<void**>(&m_spClient)));
		HR(m_spClient.QueryInterface(&m_spAdvanced));
		HR(m_spClient.QueryInterface(&m_spData));
		HR(m_spClient.QueryInterface(&m_spRegisters));
		HR(m_spClient.QueryInterface(&m_spSymbols));
		HR(m_spClient.QueryInterface(&m_spSystem));
		HR(m_spClient.QueryInterface(&m_spControl));

		HR(m_spClient->SetEventCallbacks(this));
		HR(m_spClient->SetOutputCallbacks(this));

		return S_OK;
		});
}

HRESULT Debugger::Impl::AttachToProcessInternal(DWORD pid, AttachType attach) {
	return RunAsync([this](auto pid, auto attach) {
		HR(m_spClient->AttachProcess(0, pid, static_cast<ULONG>(attach)));
		return m_spControl->WaitForEvent(0, INFINITE);
		}, pid, attach);

}

HRESULT Debugger::Impl::WaitForEventInternal(ULONG timeout) {
	return S_OK;
}

HRESULT Debugger::Impl::Create(DWORD notifyThreadId) {
	m_NotifyThreadId = notifyThreadId;
	m_hShutdownEvent = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);
	ATLASSERT(m_hShutdownEvent);
	m_hAsyncEvent = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);

	m_hDebugThread = ::CreateThread(nullptr, 0,
		[](auto p) { return ((Impl*)p)->DebuggerThread(); },
		this, 0, nullptr);
	ATLASSERT(m_hDebugThread);

	return CreateInternal();
}

HRESULT Debugger::Impl::AttachToProcess(DWORD pid, AttachType attach) {
	return AttachToProcessInternal(pid, attach);
}

HRESULT Debugger::Impl::WaitForEvent(UINT timeout) {
	return S_OK;
}

DWORD Debugger::Impl::DebuggerThread() {
	for (;;) {
		if (::WaitForSingleObjectEx(m_hShutdownEvent, INFINITE, TRUE) == WAIT_OBJECT_0)
			break;
	}

	return 0;
}

DebugEngine::Debugger::Debugger() : _impl(new CComObject<Impl>) {
}

DebugEngine::Debugger::~Debugger() {
}

HRESULT DebugEngine::Debugger::Create(DWORD notifyThreadId) {
	return _impl->Create(notifyThreadId);
}

HRESULT DebugEngine::Debugger::AttachToProcess(DWORD pid, AttachType attach) {
	return _impl->AttachToProcess(pid, attach);
}

void DebugEngine::Debugger::FreeNotification(WPARAM id) {
	return _impl->FreeNotification(id);
}
