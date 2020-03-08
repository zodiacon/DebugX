#pragma once

#include <shared_mutex>
#include "..\DebugEngine\Debugger.h"

class DebugManager final {
public:
	static DebugManager* Get();
	void Destroy();

	bool RegisterWindow(HWND hNotify);
	bool UnregisterWindow(HWND hNotify);

	DebugEngine::Debugger& Debugger();

private:
	DebugManager();
	DebugManager(const DebugManager&) = delete;
	DebugManager& operator=(const DebugManager&) = delete;

	DWORD NotifyThread();
	HRESULT Init();

	static std::unique_ptr<DebugManager> DbgMgr;
	wil::unique_handle _hNotifyThread;
	std::vector<HWND> _registeredHwnds;
	std::shared_mutex _registeredHwndsLock;
};

