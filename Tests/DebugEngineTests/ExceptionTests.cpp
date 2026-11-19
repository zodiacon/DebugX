// Exception and event filters (exc.exe)
#include "Harness.h"

using namespace DebugEngine;

static const EventFilter* FindFilter(const std::vector<EventFilter>& filters, ULONG code) {
	auto it = std::find_if(filters.begin(), filters.end(), [=](auto& f) { return f.IsException && f.Code == code; });
	return it == filters.end() ? nullptr : &*it;
}

static Task<ULONG64> Stage(DebuggerX& dbg) {
	co_return co_await Eval(dbg, L"poi(exc!g_stage) & 0xffffffff");
}

static bool Reported(ULONG code) {
	return std::find(g_Events.Exceptions.begin(), g_Events.Exceptions.end(), code) != g_Events.Exceptions.end();
}

Task<void> ExceptionSuite(DebuggerX& dbg) {
	const auto exe = Target(L"exc");
	std::vector<EventFilter> defaults;
	co_await dbg.GetEventFilters(defaults, true);
	auto av = FindFilter(defaults, 0xC0000005), eh = FindFilter(defaults, 0xE06D7363);
	auto events = std::count_if(defaults.begin(), defaults.end(), [](auto& f) { return !f.IsException; });
	Check(av && eh && events > 5 && !av->Name.empty(), "default filters: events, and exceptions with names");
	if (!av || !eh) {
		::PostQuitMessage(0);
		co_return;
	}

	// defaults: the C++ exception does not break, the access violation (first chance) does
	auto hr = co_await dbg.LaunchAndAttach(exe.c_str());
	Check(SUCCEEDED(hr) && dbg.GetTargetType() != TargetType::Unknown, "launch exc.exe: stops at the initial breakpoint");
	g_Events.Exceptions.clear();
	co_await dbg.ExecuteCommand(L"g");
	Check(co_await Stage(dbg) == 2, "defaults: stops at the access violation (stage 2), not at the C++ exception");
	Check(Reported(0xE06D7363) && Reported(0xC0000005), "both exceptions reported (Exception notification)");
	co_await dbg.Stop();

	// C++ exceptions break, access violations are ignored, a custom code breaks
	auto filters = defaults;
	for (auto& f : filters) {
		if (f.IsException && f.Code == 0xE06D7363)
			f.ExecutionOption = DEBUG_FILTER_BREAK;
		if (f.IsException && f.Code == 0xC0000005)
			f.ExecutionOption = DEBUG_FILTER_IGNORE;
	}
	EventFilter custom;
	custom.IsException = custom.IsArbitrary = true;
	custom.Code = 0xE0001234;
	custom.ExecutionOption = DEBUG_FILTER_BREAK;
	filters.push_back(custom);
	hr = co_await dbg.SetEventFilters(filters);
	Check(SUCCEEDED(hr), "SetEventFilters");

	std::vector<EventFilter> now;
	co_await dbg.GetEventFilters(now);
	auto c = FindFilter(now, 0xE0001234);
	Check(c && c->IsArbitrary && c->ExecutionOption == DEBUG_FILTER_BREAK, "a custom exception filter added");
	Check(FindFilter(now, 0xC0000005)->ExecutionOption == DEBUG_FILTER_IGNORE && FindFilter(now, 0xE06D7363)->ExecutionOption == DEBUG_FILTER_BREAK,
		"options read back");

	hr = co_await dbg.LaunchAndAttach(exe.c_str());
	Check(SUCCEEDED(hr), "relaunch");
	co_await dbg.ExecuteCommand(L"g");
	Check(co_await Stage(dbg) == 1, "the C++ exception breaks (stage 1)");
	co_await dbg.ExecuteCommand(L"g");
	Check(co_await Stage(dbg) == 3, "the access violation is ignored; the custom exception breaks (stage 3)");
	co_await dbg.Stop();

	// removing the custom one, putting the others back
	hr = co_await dbg.SetEventFilters(defaults);
	now.clear();
	co_await dbg.GetEventFilters(now);
	Check(SUCCEEDED(hr) && !FindFilter(now, 0xE0001234) && FindFilter(now, 0xC0000005)->ExecutionOption == av->ExecutionOption,
		"back to the defaults: the custom one removed");

	// the "Unknown exception" filter (code 0): every code without its own -- 0xE0001234 now -- breaks first chance
	auto unknown = defaults;
	for (auto& f : unknown) {
		if (f.IsException && f.Code == 0)
			f.ExecutionOption = DEBUG_FILTER_BREAK;
		if (f.IsException && f.Code == 0xC0000005)
			f.ExecutionOption = DEBUG_FILTER_IGNORE;
	}
	co_await dbg.SetEventFilters(unknown);
	now.clear();
	co_await dbg.GetEventFilters(now);
	auto zero = FindFilter(now, 0);
	Check(zero && zero->ExecutionOption == DEBUG_FILTER_BREAK && !zero->IsArbitrary, "the Unknown exception filter (code 0) set, not added as a new one");
	co_await dbg.LaunchAndAttach(exe.c_str());
	co_await dbg.ExecuteCommand(L"g");
	Check(co_await Stage(dbg) == 3, "Unknown exception set to break: the custom code breaks (stage 3)");
	co_await dbg.Stop();
	co_await dbg.SetEventFilters(defaults);

	// a specific event, by name
	auto specific = defaults;
	std::wstring name;
	for (auto& f : specific) {
		if (!f.IsException) {
			name = f.Name;
			f.ExecutionOption = f.ExecutionOption == DEBUG_FILTER_IGNORE ? DEBUG_FILTER_OUTPUT : DEBUG_FILTER_IGNORE;
			break;
		}
	}
	co_await dbg.SetEventFilters(specific);
	now.clear();
	co_await dbg.GetEventFilters(now);
	Check(!now.empty() && now[0].Name == name && now[0].ExecutionOption == specific[0].ExecutionOption, "a specific event's option set by name");
	co_await dbg.SetEventFilters(defaults);

	::PostQuitMessage(0);
}
