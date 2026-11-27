#pragma once

#include <string>
#include <string_view>

//
// UTF-8 (GDB/MI) <-> std::wstring (IDebugger; UTF-32 on Linux). Invalid UTF-8 bytes become U+FFFD.
//
namespace GdbEngine {
	inline std::wstring FromUtf8(std::string_view text) {
		std::wstring result;
		result.reserve(text.size());
		for (size_t i = 0; i < text.size(); ) {
			auto c = static_cast<unsigned char>(text[i]);
			char32_t cp;
			int extra;
			if (c < 0x80) { cp = c; extra = 0; }
			else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
			else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
			else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
			else { result += L'�'; i++; continue; }
			bool ok = true;
			for (int k = 1; k <= extra; k++) {
				if (i + k >= text.size() || (static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) {
					ok = false;
					break;
				}
				cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
			}
			if (!ok) {
				result += L'�';
				i++;
				continue;
			}
			result += static_cast<wchar_t>(cp);
			i += extra + 1;
		}
		return result;
	}

	inline std::string ToUtf8(std::wstring_view text) {
		std::string result;
		result.reserve(text.size());
		for (wchar_t wc : text) {
			auto cp = static_cast<char32_t>(wc);
			if (cp < 0x80)
				result += static_cast<char>(cp);
			else if (cp < 0x800) {
				result += static_cast<char>(0xC0 | (cp >> 6));
				result += static_cast<char>(0x80 | (cp & 0x3F));
			}
			else if (cp < 0x10000) {
				result += static_cast<char>(0xE0 | (cp >> 12));
				result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
				result += static_cast<char>(0x80 | (cp & 0x3F));
			}
			else {
				result += static_cast<char>(0xF0 | (cp >> 18));
				result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
				result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
				result += static_cast<char>(0x80 | (cp & 0x3F));
			}
		}
		return result;
	}
}
