#include "stdafx.h"
#include "EventFilterText.h"
#include <format>

using namespace DebugEngine;

namespace {
	const EventFilter* FindDefault(const std::vector<EventFilter>& defaults, const EventFilter& f) {
		for (auto& d : defaults) {
			if (d.IsException == f.IsException && (f.IsException ? d.Code == f.Code : d.Name == f.Name))
				return &d;
		}
		return nullptr;
	}
}

std::wstring EventFilterText::Save(const std::vector<EventFilter>& filters, const std::vector<EventFilter>& defaults) {
	std::wstring text;
	for (auto& f : filters) {
		auto d = FindDefault(defaults, f);
		if (d && !f.IsArbitrary && d->ExecutionOption == f.ExecutionOption && (!f.IsException || d->ContinueOption == f.ContinueOption))
			continue;

		if (!text.empty())
			text += L';';
		if (!f.IsException)
			text += std::format(L"e:{}={}", f.Name, f.ExecutionOption);
		else
			text += std::format(L"{}:{:X}={},{}", f.IsArbitrary ? L'a' : L'x', f.Code, f.ExecutionOption, f.ContinueOption);
	}
	return text;
}

std::vector<EventFilter> EventFilterText::Load(const std::wstring& text, const std::vector<EventFilter>& defaults) {
	auto filters = defaults;
	size_t pos = 0;
	while (pos < text.size()) {
		auto end = text.find(L';', pos);
		if (end == std::wstring::npos)
			end = text.size();
		std::wstring item = text.substr(pos, end - pos);
		pos = end + 1;

		// (an event's name may have anything in it but '=' at the end)
		auto eq = item.rfind(L'=');
		if (item.size() < 3 || item[1] != L':' || eq == std::wstring::npos)
			continue;
		const wchar_t kind = item[0];
		const std::wstring key = item.substr(2, eq - 2);
		const std::wstring value = item.substr(eq + 1);
		ULONG execution = 0, cont = DEBUG_FILTER_GO_NOT_HANDLED;
		if (::swscanf_s(value.c_str(), L"%lu,%lu", &execution, &cont) < 1 || execution > DEBUG_FILTER_IGNORE || cont > DEBUG_FILTER_GO_NOT_HANDLED)
			continue;

		if (kind == L'e') {
			for (auto& f : filters) {
				if (!f.IsException && f.Name == key) {
					f.ExecutionOption = execution;
					break;
				}
			}
			continue;
		}
		if (kind != L'x' && kind != L'a')
			continue;

		wchar_t* endp;
		const ULONG code = ::wcstoul(key.c_str(), &endp, 16);
		if (endp == key.c_str() || *endp)
			continue;
		auto it = std::find_if(filters.begin(), filters.end(), [&](auto& f) { return f.IsException && f.Code == code; });
		if (it == filters.end()) {
			EventFilter f;
			f.IsException = f.IsArbitrary = true;
			f.Code = code;
			it = filters.insert(filters.end(), f);
		}
		it->ExecutionOption = execution;
		it->ContinueOption = cont;
	}
	return filters;
}
