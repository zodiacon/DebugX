#pragma once

#include <string>

// The Anthropic API key the Assistant pane uses: in Windows Credential Manager (a generic credential of the user's,
// "DebugX/Anthropic API key"), else the ANTHROPIC_API_KEY environment variable
namespace ApiKey {
	// the key to use; empty: none
	std::wstring Get();
	// a key is stored (not just in the environment)
	bool IsStored();
	bool InEnvironment();
	bool Store(const std::wstring& key);
	void Remove();
}
