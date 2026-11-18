#pragma once

#include <string>
#include <vector>
#include "Debugger.h"

//
// The event filters the user changed (Exception Settings), as a line of text for AppSettings::EventFilters: only what
// is not as the engine has it by default, so a newer engine's defaults still apply to the rest.
//   e:<event name>=<execution>;x:<hex code>=<execution>,<continue>;a:<hex code>=<execution>,<continue>
// (a: an exception added by code)
//
namespace EventFilterText {
	std::wstring Save(const std::vector<DebugEngine::EventFilter>& filters, const std::vector<DebugEngine::EventFilter>& defaults);
	// the defaults with what 'text' says applied to them
	std::vector<DebugEngine::EventFilter> Load(const std::wstring& text, const std::vector<DebugEngine::EventFilter>& defaults);
}
