#pragma once

//
// What the Windows GUI's pch.h gives the files the Linux build shares with it (../../DebugX/SessionModel.cpp): the
// standard headers they use, and ATL's CA2W (here: UTF-8 to wide, as GDB gives its strings).
//

#include <algorithm>
#include <deque>
#include <string>
#include <vector>
#include "PlatformTypes.h"
#include "Utf8.h"

class CA2W {
public:
	explicit CA2W(const char* text) : m_Text(GdbEngine::FromUtf8(text ? text : "")) {}
	operator const wchar_t* () const {
		return m_Text.c_str();
	}

private:
	std::wstring m_Text;
};
