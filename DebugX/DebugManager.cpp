#include "pch.h"
#include "DebugManager.h"
#include "resource.h"
#include <format>

using namespace DebugEngine;

DebugManager& DebugManager::Get() {
	static DebugManager mgr;
	return mgr;
}

HRESULT DebugManager::Init() {
	ATLASSERT(!m_Debugger);
	if (m_Debugger)
		return E_UNEXPECTED;

	// dbgeng.dll is delay-loaded: the Windows Kits debugger's if it is installed (symbol servers, the !-commands), else
	// System32's, which has neither
	m_EnginePath = LoadEngine(EngineSource::Automatic);
	if (m_EnginePath.empty())
		return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);

	auto debugger = std::make_unique<DebugEngine::DebuggerX>();
	auto session = std::make_unique<SessionModel>(*debugger);
	auto hr = debugger->Create([model = session.get()](auto type, void* data) {
		model->OnNotification(type, data);
		});
	if (SUCCEEDED(hr)) {
		m_Debugger = std::move(debugger);
		m_Session = std::move(session);
	}
	return hr;
}

void DebugManager::Destroy() {
	// the debugger first: it can still be delivering
	m_Debugger.reset();
	m_Session.reset();
}

DebugEngine::DebuggerX& DebugManager::DebuggerX() {
	ATLASSERT(Get().m_Debugger);
	return *Get().m_Debugger;
}

SessionModel& DebugManager::Session() {
	ATLASSERT(Get().m_Session);
	return *Get().m_Session;
}

Task<void> DebugManager::SetNextStatement(HWND owner, ULONG64 address) {
	if (!Session().CanRun()) {
		::MessageBeep(MB_ICONWARNING);
		co_return;
	}
	auto& dbg = DebuggerX();
	auto hr = co_await dbg.SetInstructionOffset(address);
	if (hr == SetNextOtherFunction) {
		// (the window may be gone by now)
		if (!::IsWindow(owner))
			co_return;
		if (AtlMessageBox(owner, L"The new location is in another function than the current one: the stack will not be what "
			L"that code expects, and the target may well crash.\n\nSet the next statement anyway?", IDS_TITLE,
			MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES)
			co_return;
		hr = co_await dbg.SetInstructionOffset(address, true);
	}
	if (FAILED(hr) && ::IsWindow(owner))
		AtlMessageBox(owner, std::format(L"Failed to set the next statement (0x{:08X})", static_cast<unsigned>(hr)).c_str(),
			IDS_TITLE, MB_ICONERROR);
}
