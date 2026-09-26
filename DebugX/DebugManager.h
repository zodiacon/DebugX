#pragma once

#include <mutex>
#include "..\DebugEngine\Debugger.h"

class DebugManager final {
public:
	static DebugManager& Get();
	void Destroy();

	bool RegisterWindow(HWND hNotify);
	bool UnregisterWindow(HWND hNotify);
	bool IsAlive() const;

	static DebugEngine::DebuggerX& DebuggerX();
	HRESULT Init();

private:
	DebugManager();
	DebugManager(const DebugManager&) = delete;
	DebugManager& operator=(const DebugManager&) = delete;

	DWORD NotifyThread();

	static DebugManager* DbgMgr;
	wil::unique_handle m_hNotifyThread;
	std::vector<HWND> m_RegisteredHwnds;
	std::recursive_mutex m_RegisteredHwndsLock;
};

