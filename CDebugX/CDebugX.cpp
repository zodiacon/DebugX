// CDebugX.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <Windows.h>
#include <stdio.h>
#include <memory>
#include <atlbase.h>

#include "..\DebugEngine\Debugger.h"

int wmain(int argc, const wchar_t* argv[]) {
	using namespace DebugEngine;

	Debugger debugger;
	ATLVERIFY(SUCCEEDED(debugger.Create()));

}

