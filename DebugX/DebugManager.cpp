#include "stdafx.h"
#include "DebugManager.h"

using namespace DebugEngine;

DebugManager* DebugManager::DbgMgr;
static std::unique_ptr<DebuggerX> Dbg;

DebugManager& DebugManager::Get() {
	static DebugManager mgr;
	return mgr;
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

DebugEngine::DebuggerX& DebugManager::DebuggerX() {
	ATLASSERT(Dbg);
	return *Dbg;
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

HRESULT DebugManager::Init(IMessageLoop* ml) {
	if (this != DbgMgr)
		return E_UNEXPECTED;
	if (_hNotifyThread)
		return E_UNEXPECTED;

	DWORD id;
	_hNotifyThread.reset(::CreateThread(nullptr, 0, [](auto p) {
		return ((DebugManager*)p)->NotifyThread();
		}, this, 0, &id));

	Dbg = std::make_unique<DebugEngine::DebuggerX>(ml);
	return Dbg->Create(id);
}

DebugManager::DebugManager() {
	ATLASSERT(DbgMgr == nullptr);
	DbgMgr = this;
}
