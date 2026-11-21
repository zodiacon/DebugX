#include "pch.h"
#include "RecentTargets.h"
#include "AppSettings.h"
#include <Shlwapi.h>

#pragma comment(lib, "shlwapi")

namespace {
	// a line: kind, path, arguments, working directory, tab-separated
	constexpr wchar_t Separator = L'\t';

	std::wstring Clean(std::wstring text) {
		std::erase_if(text, [](wchar_t c) { return c == Separator || c == L'\n' || c == L'\r'; });
		return text;
	}

	std::vector<std::wstring> Split(std::wstring_view text, wchar_t separator) {
		std::vector<std::wstring> parts;
		while (true) {
			auto end = text.find(separator);
			parts.emplace_back(text.substr(0, end));
			if (end == std::wstring_view::npos)
				break;
			text.remove_prefix(end + 1);
		}
		return parts;
	}

	std::wstring Compact(const std::wstring& path, UINT chars = 60) {
		std::wstring text(MAX_PATH * 2, L'\0');
		if (path.size() >= text.size() || !::PathCompactPathExW(text.data(), path.c_str(), chars, 0))
			return path;
		text.resize(wcslen(text.c_str()));
		return text;
	}

	void Save(const std::vector<RecentTarget>& targets) {
		std::wstring text;
		for (auto& t : targets) {
			if (!text.empty())
				text += L'\n';
			text += std::to_wstring(static_cast<int>(t.Type)) + Separator + Clean(t.Path) + Separator + Clean(t.Arguments) + Separator + Clean(t.WorkingDir);
		}
		AppSettings::Get().RecentTargets(text);
	}
}

std::wstring RecentTarget::MenuText() const {
	std::wstring text;
	switch (Type) {
		case Kind::Run:
			text = L"Run " + Compact(Path);
			if (!Arguments.empty())
				text += L" " + (Arguments.size() > 40 ? Arguments.substr(0, 40) + L"..." : Arguments);
			break;
		case Kind::Attach: text = L"Attach to " + Path; break;
		case Kind::Dump: text = L"Dump " + Compact(Path); break;
		case Kind::Kernel: text = L"Kernel " + Path; break;
		case Kind::LocalKernel: text = L"Local Kernel"; break;
	}
	// (an & would underline the next letter)
	std::wstring escaped;
	for (auto c : text) {
		if (c == L'&')
			escaped += L'&';
		escaped += c;
	}
	return escaped;
}

std::vector<RecentTarget> RecentTargets::Load() {
	std::vector<RecentTarget> targets;
	for (auto& line : Split(AppSettings::Get().RecentTargets(), L'\n')) {
		auto parts = Split(line, Separator);
		if (parts.size() < 4 || parts[0].empty())
			continue;
		auto kind = _wtoi(parts[0].c_str());
		if (kind < 0 || kind > static_cast<int>(RecentTarget::Kind::LocalKernel))
			continue;
		RecentTarget t{ static_cast<RecentTarget::Kind>(kind), parts[1], parts[2], parts[3] };
		if (t.Path.empty() && t.Type != RecentTarget::Kind::LocalKernel)
			continue;
		targets.push_back(std::move(t));
		if (targets.size() == MaxCount)
			break;
	}
	return targets;
}

void RecentTargets::Add(RecentTarget target) {
	target.Path = Clean(std::move(target.Path));
	target.Arguments = Clean(std::move(target.Arguments));
	target.WorkingDir = Clean(std::move(target.WorkingDir));
	auto targets = Load();
	std::erase_if(targets, [&](auto& t) {
		// (the same executable with other arguments is another target; the same file in another case is not)
		return t.Type == target.Type && _wcsicmp(t.Path.c_str(), target.Path.c_str()) == 0 && t.Arguments == target.Arguments
			&& _wcsicmp(t.WorkingDir.c_str(), target.WorkingDir.c_str()) == 0;
		});
	targets.insert(targets.begin(), std::move(target));
	if (targets.size() > MaxCount)
		targets.resize(MaxCount);
	Save(targets);
}

void RecentTargets::Clear() {
	Save({});
}
