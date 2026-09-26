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
	::PostThreadMessage(::GetThreadId(m_hNotifyThread.get()), WM_QUIT, 0, 0);
	Dbg = nullptr;
	DbgMgr = nullptr;
	::WaitForSingleObject(m_hNotifyThread.get(), 1000);
}

bool DebugManager::RegisterWindow(HWND hNotify) {
	std::lock_guard locker(m_RegisteredHwndsLock);
	auto it = std::find(m_RegisteredHwnds.begin(), m_RegisteredHwnds.end(), hNotify);
	if (it != m_RegisteredHwnds.end())
		return true;
	m_RegisteredHwnds.push_back(hNotify);
	return true;
}

bool DebugManager::UnregisterWindow(HWND hNotify) {
	decltype(m_RegisteredHwnds.begin()) it;

	std::lock_guard locker(m_RegisteredHwndsLock);
	it = std::find(m_RegisteredHwnds.begin(), m_RegisteredHwnds.end(), hNotify);
	if (it == m_RegisteredHwnds.end())
		return false;
	m_RegisteredHwnds.erase(it);
	return true;
}

bool DebugManager::IsAlive() const {
	return Dbg != nullptr;
}

DebugEngine::DebuggerX& DebugManager::DebuggerX() {
	ATLASSERT(Dbg);
	return *Dbg;
}

DWORD DebugManager::NotifyThread() {
	MSG msg;
	while (::GetMessage(&msg, nullptr, 0, 0)) {
		std::lock_guard locker(m_RegisteredHwndsLock);
		for (auto h : m_RegisteredHwnds) {
			::SendMessage(h, msg.message, 0, msg.lParam);
		}
		Dbg->FreeNotification(static_cast<DebuggerNotificationType>(msg.message), msg.wParam);
	}

	return 0;
}

HRESULT DebugManager::Init() {
	if (this != DbgMgr)
		return E_UNEXPECTED;
	if (m_hNotifyThread)
		return E_UNEXPECTED;

	DWORD id;
	m_hNotifyThread.reset(::CreateThread(nullptr, 0, [](auto p) {
		return ((DebugManager*)p)->NotifyThread();
	}, this, 0, &id));

	Dbg = std::make_unique<DebugEngine::DebuggerX>();
	return Dbg->Create(id);
}

DebugManager::DebugManager() {
	ATLASSERT(DbgMgr == nullptr);
	DbgMgr = this;
}
