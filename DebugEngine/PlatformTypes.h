#pragma once

//
// The Windows base types IDebugger.h uses (ULONG, ULONG64, BYTE, DWORD, HRESULT, PCWSTR, HANDLE, the E_* codes,
// DEFINE_ENUM_FLAG_OPERATORS), for a build that has no <windows.h>. On Windows it is empty: the includer has
// <windows.h> already (DebugX's pch.h), and the types stay exactly what they are there, so DbgEng's implementation and
// the GUI see no difference.
//
// The widths are Windows' (ULONG is 32 bits, also on an LP64 Linux where unsigned long is 64), and the HRESULT values
// are the same numbers, so what an engine returns means the same thing on both. Strings stay wchar_t (UTF-16 on Windows,
// UTF-32 on Linux); an engine converts at its own edge (GDB/MI is UTF-8).
//

#ifndef _WIN32

#include <cstdint>
#include <type_traits>

using BYTE = std::uint8_t;
using USHORT = std::uint16_t;
using ULONG = std::uint32_t;
using DWORD = std::uint32_t;
using LONG = std::int32_t;
using ULONG64 = std::uint64_t;
using LONG64 = std::int64_t;
using ULONG_PTR = std::uintptr_t;
using BOOL = int;
using HRESULT = std::int32_t;
using HANDLE = void*;
using PCWSTR = const wchar_t*;
using PCSTR = const char*;

inline constexpr HRESULT S_OK = 0;
inline constexpr HRESULT S_FALSE = 1;
inline constexpr HRESULT E_NOTIMPL = static_cast<HRESULT>(0x80004001u);
inline constexpr HRESULT E_ABORT = static_cast<HRESULT>(0x80004004u);
inline constexpr HRESULT E_FAIL = static_cast<HRESULT>(0x80004005u);
inline constexpr HRESULT E_PENDING = static_cast<HRESULT>(0x8000000Au);
inline constexpr HRESULT E_UNEXPECTED = static_cast<HRESULT>(0x8000FFFFu);
inline constexpr HRESULT E_OUTOFMEMORY = static_cast<HRESULT>(0x8007000Eu);
inline constexpr HRESULT E_INVALIDARG = static_cast<HRESULT>(0x80070057u);
inline constexpr HRESULT E_ACCESSDENIED = static_cast<HRESULT>(0x80070005u);

inline constexpr ULONG SEVERITY_SUCCESS = 0;
inline constexpr ULONG SEVERITY_ERROR = 1;
inline constexpr ULONG FACILITY_ITF = 4;
inline constexpr ULONG FACILITY_WIN32 = 7;

inline constexpr ULONG ERROR_FILE_NOT_FOUND = 2;
inline constexpr ULONG ERROR_PARTIAL_COPY = 299;
inline constexpr ULONG ERROR_NOT_FOUND = 1168;
inline constexpr ULONG ERROR_CANCELLED = 1223;
inline constexpr ULONG ERROR_TIMEOUT = 1460;

constexpr HRESULT MAKE_HRESULT(ULONG severity, ULONG facility, ULONG code) {
	return static_cast<HRESULT>((severity << 31) | (facility << 16) | code);
}

constexpr HRESULT HRESULT_FROM_WIN32(ULONG error) {
	return static_cast<HRESULT>(error) <= 0 ? static_cast<HRESULT>(error) : static_cast<HRESULT>((error & 0x0000FFFF) | (FACILITY_WIN32 << 16) | 0x80000000u);
}

constexpr bool SUCCEEDED(HRESULT hr) {
	return hr >= 0;
}

constexpr bool FAILED(HRESULT hr) {
	return hr < 0;
}

#define DEFINE_ENUM_FLAG_OPERATORS(T) \
	inline constexpr T operator|(T a, T b) noexcept { return T(std::underlying_type_t<T>(a) | std::underlying_type_t<T>(b)); } \
	inline constexpr T operator&(T a, T b) noexcept { return T(std::underlying_type_t<T>(a) & std::underlying_type_t<T>(b)); } \
	inline constexpr T operator^(T a, T b) noexcept { return T(std::underlying_type_t<T>(a) ^ std::underlying_type_t<T>(b)); } \
	inline constexpr T operator~(T a) noexcept { return T(~std::underlying_type_t<T>(a)); } \
	inline constexpr T& operator|=(T& a, T b) noexcept { return a = a | b; } \
	inline constexpr T& operator&=(T& a, T b) noexcept { return a = a & b; } \
	inline constexpr T& operator^=(T& a, T b) noexcept { return a = a ^ b; }

#endif
