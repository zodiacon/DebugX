// CDebugX.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <Windows.h>
#include <stdio.h>
#include <memory>
#include <atlbase.h>

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


int wmain(int argc, const wchar_t* argv[]) {
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

		
	}
}

