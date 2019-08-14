#include "pch.h"
#include "Debugger.h"
#include <functional>

#pragma comment(lib, "dbgeng")

using namespace DebugEngine;

#define HR(x) { auto _hr = (x); if(FAILED(_hr)) return _hr; }

struct Debugger::Impl {
	Impl();
	~Impl();

	HRESULT Create();
	HRESULT AttachToProcess(DWORD pid, AttachType attach);

private:
	template<typename... Args>
	using ActionType = std::function<HRESULT(Args...)>;
		
	template<typename... Args>
	struct ApcData {
		Impl* This;
		ActionType<Args...> Action;
		HANDLE hEvent;
		HRESULT HResult;
	};


	HRESULT WaitForEvent(UINT timeout = INFINITE);
	DWORD DebuggerThread();

	template<typename T1>
	HRESULT RunAsync(ActionType<T1> action, T1 arg) {
		ApcData<T1> data;
		data.This = this;
		data.Action = action;

		::QueueUserAPC([](auto p) {
			auto d = (decltype(data)*)p;
			d->HResult = (d->This->*(d->Action))(arg);
			}, m_hDebugThread, (ULONG_PTR)& data);
		::WaitForSingleObject(data.hEvent, INFINITE);

		return data.HResult;
	}

	template<typename T1, typename T2>
	HRESULT RunAsync(ActionType<T1, T2> action, T1 arg1, T2 arg2) {
		ApcData<T1, T2> data;
		data.This = this;
		data.Action = action;

		::QueueUserAPC([](auto p) {
			auto d = (decltype(data)*)p;
			d->HResult = (d->This->*(d->Action))(arg1, arg2);
			}, m_hDebugThread, (ULONG_PTR)& data);
		::WaitForSingleObject(data.hEvent, INFINITE);

		return data.HResult;
	}

	template<typename T1, typename T2, typename T3>
	HRESULT RunAsync(ActionType<T1, T2, T3> action, T1 arg1, T2 arg2, T3 arg3) {
		ApcData<T1, T2, T3> data;
		data.This = this;
		data.Action = action;

		::QueueUserAPC([](auto p) {
			auto d = (decltype(data)*)p;
			d->HResult = (d->This->*(d->Action))(arg1, arg2, arg3);
			}, m_hDebugThread, (ULONG_PTR)& data);
		::WaitForSingleObject(data.hEvent, INFINITE);

		return data.HResult;
	}

	HRESULT RunAsync(ActionType<void> action) {
		ApcData<void> data;
		data.This = this;
		data.Action = action;

		::QueueUserAPC([](auto p) {
			auto d = (decltype(data)*)p;
			d->HResult = d->Action();
			}, m_hDebugThread, (ULONG_PTR)& data);
		::WaitForSingleObject(data.hEvent, INFINITE);

		return data.HResult;
	}


	//template<typename... Args>
	//HRESULT RunAsync(ActionType<Args...> action, Args... args) {
	//	ApcData<Args...> data;
	//	data.This = this;
	//	data.Action = action;

	//	::QueueUserAPC([](auto p) {
	//		auto d = (decltype(data)*)p;
	//		d->HResult = (d->This->*(d->Action))(args...);
	//		}, m_hDebugThread, (ULONG_PTR)&data);
	//	::WaitForSingleObject(data.hEvent, INFINITE);

	//	return data.HResult;
	//}

	HRESULT CreateInternal();
	HRESULT AttachToProcessInternal(DWORD pid, AttachType attach);

private:
	HANDLE m_hDebugThread;
	HANDLE m_hShutdownEvent;
	CComPtr<IDebugClient7> m_spClient;
	CComPtr<IDebugAdvanced4> m_spAdvanced;
	CComPtr<IDebugDataSpaces4> m_spData;
	CComPtr<IDebugRegisters2> m_spRegisters;
	CComPtr<IDebugSystemObjects4> m_spSystem;
	CComPtr<IDebugSymbols5> m_spSymbols;
	CComPtr<IDebugControl7> m_spControl;
};

Debugger::Impl::Impl() {
}

Debugger::Impl::~Impl() {
	::SetEvent(m_hShutdownEvent);
	::WaitForSingleObject(m_hDebugThread, 1000);
	::CloseHandle(m_hShutdownEvent);
	::CloseHandle(m_hDebugThread);
}

HRESULT Debugger::Impl::CreateInternal() {
	HR(::DebugCreate(__uuidof(IDebugClient7), reinterpret_cast<void**>(&m_spClient)));
	HR(m_spClient.QueryInterface(&m_spAdvanced));
	HR(m_spClient.QueryInterface(&m_spData));
	HR(m_spClient.QueryInterface(&m_spRegisters));
	HR(m_spClient.QueryInterface(&m_spSymbols));
	HR(m_spClient.QueryInterface(&m_spSystem));
	HR(m_spClient.QueryInterface(&m_spControl));

	return S_OK;
}

HRESULT Debugger::Impl::AttachToProcessInternal(DWORD pid, AttachType attach) {
	HR(m_spClient->AttachProcess(0, pid, static_cast<ULONG>(attach)));

	return S_OK;
}

HRESULT Debugger::Impl::Create() {
	m_hShutdownEvent = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);
	ATLASSERT(m_hShutdownEvent);

	m_hDebugThread = ::CreateThread(nullptr, 0,
		[](auto p) { return ((Impl*)p)->DebuggerThread(); },
		this, 0, nullptr);
	ATLASSERT(m_hDebugThread);

	HR(RunAsync([this]() { return CreateInternal(); }));
	return S_OK;
}

HRESULT Debugger::Impl::AttachToProcess(DWORD pid, AttachType attach) {
	HR(RunAsync([this](auto pid, auto attach) { 
		return AttachToProcessInternal(pid, attach); 
		}, pid, attach));

	WaitForEvent();

	return S_OK;
}

HRESULT Debugger::Impl::WaitForEvent(UINT timeout) {
	return m_spControl->WaitForEvent(0, timeout);
}

DWORD Debugger::Impl::DebuggerThread() {
	for (;;) {
		if (::WaitForSingleObjectEx(m_hShutdownEvent, INFINITE, TRUE) == WAIT_OBJECT_0)
			break;
	}

	return 0;
}

//HRESULT Debugger::Impl::RunAsync(HRESULT (Impl::* action)()) {
//	static ApcData data;
//	data.Action = action;
//	if (data.This == nullptr) {
//		data.This = this;
//		data.hEvent = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
//		ATLASSERT(data.hEvent);
//	}
//	::QueueUserAPC(
//		[](auto a) { 
//			auto apcData = (ApcData*)a;
//			apcData->HResult = (apcData->This->*apcData->Action)();
//			::SetEvent(apcData->hEvent);
//		}, m_hDebugThread, (ULONG_PTR)&data);
//
//	::WaitForSingleObject(data.hEvent, INFINITE);
//	return data.HResult;
//}

DebugEngine::Debugger::Debugger() : _impl(new Impl) {
}

DebugEngine::Debugger::~Debugger() {
}

HRESULT DebugEngine::Debugger::Create() {
	return _impl->Create();
}

HRESULT DebugEngine::Debugger::AttachToProcess(DWORD pid, AttachType attach) {
	return _impl->AttachToProcess(pid, attach);
}
