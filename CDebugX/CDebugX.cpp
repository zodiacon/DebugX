// CDebugX.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <Windows.h>
#include <stdio.h>
#include <memory>
#include <atlbase.h>
#include <DbgEng.h>
#include <atlcom.h>

#include "..\DebugEngine\Debugger.h"

#define HR(x) ATLVERIFY(SUCCEEDED(x))

using namespace DebugEngine;

std::unique_ptr<Debugger> debugger;

DWORD CALLBACK NotifyThread(PVOID) {
	MSG msg;
	while (::GetMessage(&msg, nullptr, 0, 0)) {
		ATLASSERT(msg.hwnd == nullptr);

		switch (static_cast<DebuggerNotificationType>(msg.message)) {
			case DebuggerNotificationType::ProcessCreated:
				debugger->FreeNotification(msg.wParam);
				break;

			case DebuggerNotificationType::Output:
				auto data = reinterpret_cast<OutputData*>(msg.lParam);
				switch (data->Mask) {
					case OutputDataMask::Prompt:
						printf("%s> ", data->Text.c_str());
						break;

					default:
						printf("%s", data->Text.c_str());
						break;
				}
				debugger->FreeNotification(msg.wParam);
				break;
		}
	}
	return 0;
}

struct CDebugCallbacks :
	public CComObjectRoot,
	public IDebugEventCallbacks,
	public IDebugOutputCallbacks {

	BEGIN_COM_MAP(CDebugCallbacks)
		COM_INTERFACE_ENTRY(IDebugEventCallbacks)
		COM_INTERFACE_ENTRY(IDebugOutputCallbacks)
	END_COM_MAP()

	void SetControl(IDebugControl5* ctl) {
		m_spControl = ctl;
	}

	// Inherited via IDebugEventCallbacks
	bool BreakpointHit = false;
	bool StateChanged = false;
	ULONG Status;
	CComPtr<IDebugControl5> m_spControl;

	ULONG UpdateState() {
		m_spControl->GetExecutionStatus(&Status);
		ATLTRACE(L"Execution Status: %d\n", Status);
		return Status;
	}

	virtual HRESULT __stdcall GetInterestMask(PULONG Mask) override {
		*Mask = DEBUG_EVENT_BREAKPOINT | DEBUG_EVENT_CHANGE_DEBUGGEE_STATE | DEBUG_EVENT_CREATE_PROCESS | DEBUG_EVENT_CREATE_THREAD
			| DEBUG_EVENT_EXCEPTION | DEBUG_EVENT_LOAD_MODULE | DEBUG_EVENT_SYSTEM_ERROR | DEBUG_EVENT_CHANGE_ENGINE_STATE | DEBUG_EVENT_UNLOAD_MODULE
			| DEBUG_EVENT_EXIT_PROCESS | DEBUG_EVENT_EXIT_THREAD | DEBUG_EVENT_SESSION_STATUS;

		return S_OK;
	}

	virtual HRESULT __stdcall Breakpoint(PDEBUG_BREAKPOINT Bp) override {
		BreakpointHit = true;
		StateChanged = true;

		return DEBUG_STATUS_BREAK;
	}
	virtual HRESULT __stdcall Exception(PEXCEPTION_RECORD64 Exception, ULONG FirstChance) override {
		BreakpointHit = Exception->ExceptionCode == 0x80000004 || Exception->ExceptionCode == 0x80000003 || FirstChance == 0;

		return BreakpointHit ? DEBUG_STATUS_BREAK : DEBUG_STATUS_NO_CHANGE;
	}

	virtual HRESULT __stdcall CreateThread(ULONG64 Handle, ULONG64 DataOffset, ULONG64 StartOffset) override {
		return DEBUG_STATUS_NO_CHANGE;
	}
	virtual HRESULT __stdcall ExitThread(ULONG ExitCode) override {
		return DEBUG_STATUS_NO_CHANGE;
	}
	virtual HRESULT __stdcall ExitProcess(ULONG ExitCode) override {
		return DEBUG_STATUS_NO_CHANGE;
	}
	virtual HRESULT __stdcall LoadModule(ULONG64 ImageFileHandle, ULONG64 BaseOffset, ULONG ModuleSize, PCSTR ModuleName, PCSTR ImageName, ULONG CheckSum, ULONG TimeDateStamp) override {
		return DEBUG_STATUS_NO_CHANGE;
	}
	virtual HRESULT __stdcall UnloadModule(PCSTR ImageBaseName, ULONG64 BaseOffset) override {
		return DEBUG_STATUS_NO_CHANGE;
	}
	virtual HRESULT __stdcall SystemError(ULONG Error, ULONG Level) override {
		return DEBUG_STATUS_BREAK;
	}
	virtual HRESULT __stdcall SessionStatus(ULONG Status) override {
		m_spControl->GetExecutionStatus(&Status);
		return S_OK;
	}
	virtual HRESULT __stdcall ChangeDebuggeeState(ULONG Flags, ULONG64 Argument) override {
		StateChanged = true;
		m_spControl->GetExecutionStatus(&Status);
		
		return S_OK;
	}
	virtual HRESULT __stdcall ChangeEngineState(ULONG Flags, ULONG64 Argument) override {
		StateChanged = true;
		m_spControl->GetExecutionStatus(&Status);

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

		return DEBUG_STATUS_NO_CHANGE;
	}


	// Inherited via IDebugOutputCallbacks
	virtual HRESULT __stdcall Output(ULONG Mask, PCSTR Text) override {
		auto data = new OutputData;
		data->Text = Text;
		data->Mask = static_cast<OutputDataMask>(Mask);
		printf("%s", Text);

		return S_OK;
	}
};

IDebugControl5* g_pControl;

void RunSimpleDebugger() {
	::CoInitialize(nullptr);

	STARTUPINFO si = { sizeof(si) };
	PROCESS_INFORMATION pi;
	WCHAR name[] = L"notepad";
	if (::CreateProcess(nullptr, name, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
		::CloseHandle(pi.hProcess);
		::CloseHandle(pi.hThread);

		CComPtr<IDebugClient7> spClient;
		auto hr = ::DebugCreate(__uuidof(IDebugClient7), reinterpret_cast<void**>(&spClient));
		ATLASSERT(SUCCEEDED(hr));
		CComQIPtr<IDebugControl5> spControl(spClient);
		ATLASSERT(spControl);

		CComObject<CDebugCallbacks>* cb;
		cb->CreateInstance(&cb);
		cb->SetControl(spControl);
		spClient->SetEventCallbacks(cb);
		spClient->SetOutputCallbacks(cb);
		g_pControl = spControl;
		spControl->AddEngineOptions(DEBUG_ENGOPT_INITIAL_BREAK);

		::SetConsoleCtrlHandler([](auto type) {
			if (type == CTRL_C_EVENT) {
				g_pControl->SetInterrupt(DEBUG_INTERRUPT_ACTIVE);
				return TRUE;
			}
			return FALSE;
			}, TRUE);

		//WCHAR name[] = L"c:\\windows\\system32\\notepad.exe";
		//hr = spClient->CreateProcessAndAttachWide(0, name, DEBUG_PROCESS, 0, DEBUG_ATTACH_DEFAULT);
		spClient->AttachProcess(0, pi.dwProcessId, DEBUG_ATTACH_DEFAULT);
		spControl->WaitForEvent(0, INFINITE);

		ULONG status;

		while (true) {
			status = cb->UpdateState();
			if (status == DEBUG_STATUS_NO_DEBUGGEE) {
				printf("No target\n");
				break;
			}

			if (status == DEBUG_STATUS_GO || status == DEBUG_STATUS_STEP_BRANCH || status == DEBUG_STATUS_STEP_INTO || status == DEBUG_STATUS_STEP_OVER) {
				hr = spControl->WaitForEvent(DEBUG_WAIT_DEFAULT, INFINITE);
				continue;
			}

			if (cb->StateChanged) {
				printf("\n");
				cb->StateChanged = false;
				if (cb->BreakpointHit) {
					spControl->OutputCurrentState(DEBUG_OUTCTL_THIS_CLIENT, DEBUG_CURRENT_DEFAULT);
					cb->BreakpointHit = false;
				}
			}

			spControl->OutputPrompt(DEBUG_OUTCTL_THIS_CLIENT, nullptr);
			printf(" ");
			char cmd[256];
			gets_s(cmd);
			spControl->Execute(DEBUG_OUTCTL_THIS_CLIENT, cmd, DEBUG_EXECUTE_DEFAULT);
		}
	}
}


int wmain(int argc, const wchar_t* argv[]) {
	//RunSimpleDebugger();
	//return 0;

	DWORD id;
	auto hNotify = ::CreateThread(nullptr, 0, NotifyThread, nullptr, 0, &id);

	debugger = std::make_unique<Debugger>();

	STARTUPINFO si = { sizeof(si) };
	PROCESS_INFORMATION pi;
	WCHAR name[] = L"notepad";
	if (::CreateProcess(nullptr, name, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
		::CloseHandle(pi.hProcess);
		::CloseHandle(pi.hThread);

		HR(debugger->Create(id));
		HR(debugger->AttachToProcess(pi.dwProcessId, AttachType::Default));
		
		do {
			char command[256];
			gets_s(command);

			debugger->ExecuteCommand(command);
		} while (true);
	}
}

