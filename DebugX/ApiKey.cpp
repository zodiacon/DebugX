#include "pch.h"
#include "ApiKey.h"
#include <wincred.h>

#pragma comment(lib, "advapi32.lib")

namespace {
	PCWSTR const Target = L"DebugX/Anthropic API key";

	std::wstring Stored() {
		PCREDENTIALW credential = nullptr;
		if (!::CredReadW(Target, CRED_TYPE_GENERIC, 0, &credential))
			return {};
		std::wstring key(reinterpret_cast<const wchar_t*>(credential->CredentialBlob), credential->CredentialBlobSize / sizeof(wchar_t));
		::CredFree(credential);
		return key;
	}

	std::wstring Environment() {
		wchar_t key[512];
		auto n = ::GetEnvironmentVariableW(L"ANTHROPIC_API_KEY", key, _countof(key));
		return n > 0 && n < _countof(key) ? std::wstring(key, n) : std::wstring();
	}
}

std::wstring ApiKey::Get() {
	auto key = Stored();
	return key.empty() ? Environment() : key;
}

bool ApiKey::IsStored() {
	return !Stored().empty();
}

bool ApiKey::InEnvironment() {
	return !Environment().empty();
}

bool ApiKey::Store(const std::wstring& key) {
	CREDENTIALW credential{};
	credential.Type = CRED_TYPE_GENERIC;
	credential.TargetName = const_cast<LPWSTR>(Target);
	credential.CredentialBlobSize = static_cast<DWORD>(key.size() * sizeof(wchar_t));
	credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<wchar_t*>(key.data()));
	// (this user's, on this computer)
	credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
	credential.UserName = const_cast<LPWSTR>(L"DebugX");
	return ::CredWriteW(&credential, 0) != FALSE;
}

void ApiKey::Remove() {
	::CredDeleteW(Target, CRED_TYPE_GENERIC, 0);
}
