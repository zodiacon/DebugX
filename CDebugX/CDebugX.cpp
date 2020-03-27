// CDebugX.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <Windows.h>
#include <stdio.h>
#include <memory>
#include <atlbase.h>
#include <string>
#include <atlstr.h>
#include <DbgEng.h>
#include <atlcom.h>
#include "DebugX.Kernel.h"

using namespace DebugX::Analysis;

#include "..\DebugEngine\Debugger.h"

#define HR(x) ATLVERIFY(SUCCEEDED(x))

using namespace DebugEngine;

std::unique_ptr<Debugger> debugger;

CComModule _Module;

enum class ConsoleColor : short {
	Black,
	DarkBlue,
	DarkGreen,
	DarkCyan,
	DarkRed,
	DarkMagenta,
	DarkYellow,
	Gray,
	DarkGray,
	Blue,
	Green,
	Cyan,
	Red,
	Magenta,
	Yellow,
	White
};

void SetConsoleForegroundColor(ConsoleColor color) {
	static HANDLE hOutput;
	if (hOutput == nullptr)
		hOutput = ::GetStdHandle(STD_OUTPUT_HANDLE);

	CONSOLE_SCREEN_BUFFER_INFO info;
	GetConsoleScreenBufferInfo(hOutput, &info);
	SetConsoleTextAttribute(hOutput, (info.wAttributes & ~0x0f) | (WORD)color);
}

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
						printf("%ws> ", data->Text.c_str());
						break;

					default:
						printf("%ws", data->Text.c_str());
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
	public IDebugOutputCallbacksWide {

	BEGIN_COM_MAP(CDebugCallbacks)
		COM_INTERFACE_ENTRY(IDebugEventCallbacks)
		COM_INTERFACE_ENTRY(IDebugOutputCallbacksWide)
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


	// Inherited via IDebugOutputCallbacksWide
	HRESULT __stdcall Output(ULONG Mask, PCWSTR Text) override {
		auto data = new OutputData;
		data->Text = Text;
		data->Mask = static_cast<OutputDataMask>(Mask);
		auto color = ConsoleColor::White;
		switch (data->Mask) {
			case OutputDataMask::Prompt: color = ConsoleColor::Cyan; break;
			case OutputDataMask::Error: color = ConsoleColor::Red; break;
			case OutputDataMask::Symbols: color = ConsoleColor::Blue; break;
			case OutputDataMask::Debuggee: color = ConsoleColor::Yellow; break;
			case OutputDataMask::ExtensionWarning: color = ConsoleColor::Gray; break;
			case OutputDataMask::Warning: color = ConsoleColor::DarkYellow; break;
		}
		SetConsoleForegroundColor(color);
		printf("%ws", Text);

		return S_OK;
	}
};

IDebugControl5* g_pControl;

HRESULT RunSimpleDebugger2(int argc, wchar_t* argv[]) {
	auto pid = _wtoi(argv[1]);

	CComPtr<IDebugClient7> spClient;
	auto hr = ::DebugCreate(__uuidof(IDebugClient7), reinterpret_cast<void**>(&spClient));
	ATLASSERT(SUCCEEDED(hr));
	CComQIPtr<IDebugControl5> spControl(spClient);
	ATLASSERT(spControl);
	CComQIPtr<IDebugDataSpaces4> spData(spClient);
	ATLASSERT(spData);

	CComObject<CDebugCallbacks>* cb;
	cb->CreateInstance(&cb);
	cb->SetControl(spControl);
	spClient->SetEventCallbacks(cb);
	spClient->SetOutputCallbacksWide(cb);
	g_pControl = spControl;
	spControl->AddEngineOptions(DEBUG_ENGOPT_INITIAL_BREAK);

	::SetConsoleCtrlHandler([](auto type) {
		if (type == CTRL_C_EVENT) {
			g_pControl->SetInterrupt(DEBUG_INTERRUPT_ACTIVE);
			return TRUE;
		}
		return FALSE;
		}, TRUE);

	if (pid > 0) {
		hr = spClient->AttachProcess(0, pid, DEBUG_ATTACH_DEFAULT);
	}
	else {
		if (::_wcsicmp(argv[1], L"-lk") == 0)
			hr = spClient->AttachKernel(DEBUG_ATTACH_LOCAL_KERNEL, nullptr);
		else
			hr = spClient->CreateProcessAndAttachWide(0, argv[1], DEBUG_PROCESS, 0, DEBUG_ATTACH_DEFAULT);
	}
	if (FAILED(hr))
		return hr;

	spControl->WaitForEvent(0, INFINITE);

	//CComQIPtr<IDebugSymbols3> spSymbols(spClient);
	//ATLASSERT(spSymbols);
	//ULONG64 base;
	//hr = spSymbols->GetModuleByModuleName("nt", 0, nullptr, &base);
	//ULONG64 processListHead;
	//hr = spSymbols->GetOffsetByName("nt!PsActiveProcessHead", &processListHead);
	//ULONG id;
	//hr = spSymbols->GetTypeId(base, "_EPROCESS", &id);
	//ULONG fieldOffset;
	//hr = spSymbols->GetFieldOffset(base, id, "ActiveProcessLinks", &fieldOffset);
	//LIST_ENTRY firstProcessLink;
	//hr = spData->ReadVirtual(processListHead, &firstProcessLink, sizeof(firstProcessLink), nullptr);
	//auto pProcess = (ULONG64)firstProcessLink.Flink - fieldOffset;

	//DEBUG_VALUE value;
	//hr = spControl->Evaluate("nt!PsActiveProcessHead", DEBUG_VALUE_INVALID, &value, nullptr);

	//KernelProcessEnumerator kpe(CComQIPtr<IDebugClient>(spClient).p);
	//KernelProcessAnalyzer* p;
	//while (p = kpe.GetNextProcess()) {
	//	auto pid = p->GetProcessId();
	//	printf("PID: %5u %ws\n", pid, (PCWSTR)p->GetImageName());
	//}

	ULONG status;
	char cmd[512];

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
		SetConsoleForegroundColor(ConsoleColor::White);
		printf(" ");
		gets_s(cmd);
		spControl->Execute(DEBUG_OUTCTL_THIS_CLIENT, cmd, DEBUG_EXECUTE_DEFAULT);
	}
	return S_OK;
}

HRESULT RunSimpleDebugger(int argc, wchar_t* argv[]) {
	auto pid = _wtoi(argv[1]);

	::SetConsoleCtrlHandler([](auto type) {
		if (type == CTRL_C_EVENT) {
			g_pControl->SetInterrupt(DEBUG_INTERRUPT_ACTIVE);
			return TRUE;
		}
		return FALSE;
		}, TRUE);

	return S_OK;
}

int wmain(int argc, wchar_t* argv[]) {
	if (argc < 2) {
		printf("Usage: cdebux <pid or exePath>\n");
		return 0;
	}
	::CoInitialize(nullptr);
	auto hr = RunSimpleDebugger2(argc, argv);
	if (FAILED(hr)) {
		printf("Error: HRESULT=0x%08X\n", hr);
	}
	::CoUninitialize();
	return 0;
}

