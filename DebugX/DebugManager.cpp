#include "stdafx.h"
#include "DebugManager.h"

using namespace DebugEngine;

std::unique_ptr<DebugManager> DebugManager::DbgMgr;
static std::unique_ptr<Debugger> Dbg;

DebugManager* DebugManager::Get() {
	if (DbgMgr == nullptr) {
		DbgMgr.reset(new DebugManager);
		auto hr = DbgMgr->Init();
		if (FAILED(hr)) {
			DbgMgr = nullptr;
		}
	}
	return DbgMgr.get();
}

void DebugManager::Destroy() {
	::PostThreadMessage(::GetThreadId(_hNotifyThread.get()), WM_QUIT, 0, 0);
	Dbg = nullptr;
	DbgMgr = nullptr;
	::WaitForSingleObject(_hNotifyThread.get(), 1000);
}

bool DebugManager::RegisterWindow(HWND hNotify) {
	std::lock_guard locker(_registeredHwndsLock);
	auto it = std::find(_registeredHwnds.begin(), _registeredHwnds.end(), hNotify);
	if (it != _registeredHwnds.end())
		return true;
	_registeredHwnds.push_back(hNotify);
	return true;
}

bool DebugManager::UnregisterWindow(HWND hNotify) {
	std::lock_guard locker(_registeredHwndsLock);
	auto it = std::find(_registeredHwnds.begin(), _registeredHwnds.end(), hNotify);
	if (it == _registeredHwnds.end())
		return false;
	_registeredHwnds.erase(it);
	return true;
}

DebugEngine::Debugger& DebugManager::Debugger() {
	Get();
	return *Dbg;
}

DebugManager::DebugManager() {
	_registeredHwnds.reserve(16);
}

DWORD DebugManager::NotifyThread() {
	MSG msg;
	while (::GetMessage(&msg, nullptr, 0, 0)) {
		std::shared_lock locker(_registeredHwndsLock);
		for (auto h : _registeredHwnds) {
			::SendMessage(h, msg.message, 0, msg.lParam);
		}
		Dbg->FreeNotification(msg.wParam);
	}

	return 0;
}

HRESULT DebugManager::Init() {
	DWORD id;
	_hNotifyThread.reset(::CreateThread(nullptr, 0, [](auto p) {
		return ((DebugManager*)p)->NotifyThread();
		}, this, 0, &id));

	Dbg = std::make_unique<DebugEngine::Debugger>();
	return Dbg->Create(id);
}
