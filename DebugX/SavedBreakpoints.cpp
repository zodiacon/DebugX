#include "stdafx.h"
#include "SavedBreakpoints.h"
#include "ScintillaCtrl.h"
#include <format>
#include <fstream>
#include <sstream>

using namespace DebugEngine;

namespace {
	// The file: a block for each program, the latest first -- a line ">key", then a line for each breakpoint: expression,
	// condition, command, pass count, enabled; tab-separated, with \t, \n and \\ for those characters. UTF-8.
	struct Program {
		std::wstring Key;
		std::vector<BreakpointSettings> Breakpoints;
	};

	std::wstring FilePath() {
		wil::unique_cotaskmem_string folder;
		if (FAILED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &folder)))
			return {};
		std::wstring dir(folder.get());
		dir += L"\\DebugX";
		::CreateDirectory(dir.c_str(), nullptr);
		return dir + L"\\breakpoints.txt";
	}

	std::wstring Escape(const std::wstring& text) {
		std::wstring result;
		for (auto c : text) {
			switch (c) {
				case L'\\': result += L"\\\\"; break;
				case L'\t': result += L"\\t"; break;
				case L'\n': result += L"\\n"; break;
				case L'\r': break;
				default: result += c; break;
			}
		}
		return result;
	}

	std::vector<std::wstring> SplitEscaped(const std::wstring& line) {
		std::vector<std::wstring> fields(1);
		for (size_t i = 0; i < line.size(); i++) {
			auto c = line[i];
			if (c == L'\t')
				fields.emplace_back();
			else if (c == L'\\' && i + 1 < line.size()) {
				auto next = line[++i];
				fields.back() += next == L't' ? L'\t' : next == L'n' ? L'\n' : next;
			}
			else
				fields.back() += c;
		}
		return fields;
	}

	std::vector<Program> LoadAll() {
		std::vector<Program> programs;
		auto path = FilePath();
		if (path.empty())
			return programs;
		std::ifstream file(path, std::ios::binary);
		if (!file)
			return programs;
		std::string line;
		while (std::getline(file, line)) {
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			auto text = FromUtf8(line);
			if (text.empty())
				continue;
			if (text[0] == L'>') {
				programs.push_back({ text.substr(1) });
				continue;
			}
			auto fields = SplitEscaped(text);
			if (programs.empty() || fields.size() < 5 || fields[0].empty())
				continue;
			BreakpointSettings bp;
			bp.Expression = fields[0];
			bp.Condition = fields[1];
			bp.Command = fields[2];
			bp.PassCount = (std::max)(1ul, wcstoul(fields[3].c_str(), nullptr, 10));
			bp.Enabled = fields[4] != L"0";
			programs.back().Breakpoints.push_back(std::move(bp));
		}
		return programs;
	}

	void SaveAll(const std::vector<Program>& programs) {
		auto path = FilePath();
		if (path.empty())
			return;
		std::wstring text;
		for (auto& p : programs) {
			text += L">" + p.Key + L"\n";
			for (auto& bp : p.Breakpoints)
				text += std::format(L"{}\t{}\t{}\t{}\t{}\n", Escape(bp.Expression), Escape(bp.Condition), Escape(bp.Command), bp.PassCount, bp.Enabled ? 1 : 0);
		}
		// (a new file, then in place of the old one: a failure halfway does not lose them all)
		auto temp = path + L".new";
		{
			std::ofstream file(temp, std::ios::binary | std::ios::trunc);
			if (!file)
				return;
			auto utf8 = ToUtf8(text);
			file.write(utf8.data(), utf8.size());
			if (!file)
				return;
		}
		::MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
	}
}

std::wstring SavedBreakpoints::KeyOf(const std::wstring& exePath) {
	if (exePath.empty())
		return {};
	std::wstring full(MAX_PATH * 2, L'\0');
	auto len = ::GetFullPathNameW(exePath.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
	if (len == 0 || len >= full.size())
		full = exePath;
	else
		full.resize(len);
	::CharLowerBuffW(full.data(), static_cast<DWORD>(full.size()));
	return full;
}

std::wstring SavedBreakpoints::ImagePathOf(DWORD pid) {
	wil::unique_handle process(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
	if (!process)
		return {};
	std::wstring path(MAX_PATH * 2, L'\0');
	DWORD size = static_cast<DWORD>(path.size());
	if (!::QueryFullProcessImageNameW(process.get(), 0, path.data(), &size))
		return {};
	path.resize(size);
	return path;
}

bool SavedBreakpoints::IsKept(const BreakpointSettings& bp) {
	return !bp.IsData && !bp.OneShot && !bp.Expression.empty();
}

std::vector<BreakpointSettings> SavedBreakpoints::Load(const std::wstring& key) {
	if (key.empty())
		return {};
	for (auto& p : LoadAll()) {
		if (p.Key == key)
			return p.Breakpoints;
	}
	return {};
}

void SavedBreakpoints::Save(const std::wstring& key, const std::vector<BreakpointInfo>& breakpoints) {
	if (key.empty())
		return;
	Program program{ key };
	for (auto& bp : breakpoints) {
		if (!IsKept(bp))
			continue;
		BreakpointSettings settings = bp;
		if (!bp.Location.empty())
			settings.Expression = bp.Location;
		// (a thread's id is the session's)
		settings.ThreadId = DEBUG_ANY_ID;
		program.Breakpoints.push_back(std::move(settings));
	}

	auto programs = LoadAll();
	auto it = std::find_if(programs.begin(), programs.end(), [&](auto& p) { return p.Key == key; });
	if (it == programs.end() && program.Breakpoints.empty())
		return;		// (none, as before)
	if (it != programs.end()) {
		// (nothing to write if nothing changed)
		auto same = it->Breakpoints.size() == program.Breakpoints.size() && std::equal(it->Breakpoints.begin(), it->Breakpoints.end(),
			program.Breakpoints.begin(), [](auto& a, auto& b) {
				return a.Expression == b.Expression && a.Condition == b.Condition && a.Command == b.Command && a.PassCount == b.PassCount && a.Enabled == b.Enabled;
			});
		if (same && it == programs.begin())
			return;
		programs.erase(it);
	}
	// (a program without any is forgotten)
	if (!program.Breakpoints.empty())
		programs.insert(programs.begin(), std::move(program));
	if (programs.size() > MaxPrograms)
		programs.resize(MaxPrograms);
	SaveAll(programs);
}
