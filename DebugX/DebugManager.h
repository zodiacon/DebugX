#pragma once

#include <shared_mutex>
#include "..\DebugEngine\Debugger.h"

class DebugManager final {
public:
	static DebugManager* Get();
	void Destroy();

	bool RegisterWindow(HWND hNotify);
	bool UnregisterWindow(HWND hNotify);

	static DebugEngine::Debugger& Debugger();
	DebugManager();
	HRESULT Init(DebugEngine::IMessageLoop* ml);

private:
	DebugManager(const DebugManager&) = delete;
	DebugManager& operator=(const DebugManager&) = delete;

	DWORD NotifyThread();

	static DebugManager* DbgMgr;
	DebugEngine::IMessageLoop* _ml{ nullptr };
	wil::unique_handle _hNotifyThread;
	std::vector<HWND> _registeredHwnds;
	std::shared_mutex _registeredHwndsLock;
};

