// pch.h : include file for standard system include files,
//  or project specific include files that are used frequently, but
//  are changed infrequently
//

#pragma once

#ifndef _WIN32
// (the Linux build compiles a few files of this project too, SessionModel.cpp: this is its stand-in for the rest of this header)
#include "../Linux/Shared/SharedPch.h"
#else

// Change these values to use different versions
// Windows 10 1703 (Creators Update) is the minimum: the docking framework needs per-monitor DPI
#define WINVER		0x0A00
#define _WIN32_WINNT	0x0A00
#define _WIN32_IE	0x0A00
#define _RICHEDIT_VER	0x0700
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <atlbase.h>
#include <atlapp.h>
#include <atlstr.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlframe.h>
#include <atlctrls.h>
#include <atldlgs.h>
#include <atlctrlw.h>
#include <atlctrlx.h>
#include <atlgdi.h>
#include <atluser.h>
#include <uxtheme.h>
#include <ShlObj.h>
#include <string>
#include <DbgEng.h>
#include <vector>
#include <memory>
#include <wil\resource.h>
#include <atltime.h>
#include <atlmisc.h>
#include <wrl.h>
#include <wrl/client.h>
#include <wrl/implements.h>
#include <wrl/module.h>
#include <algorithm>
#include <DbgModel.h>
#include "DbgModelClientEx.h"
#include <WTLDockUI.h>
#include <WTLHelper.h>

#pragma comment(lib, "uxtheme.lib")

#if defined _M_IX86
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_IA64
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='ia64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_X64
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#else
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif
#endif
