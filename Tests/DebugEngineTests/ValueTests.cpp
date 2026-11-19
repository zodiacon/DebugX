// How values are shown (the data model: visualizers for the standard library's types, fields, elements, pointers) and
// assigned (values.exe)
#include "Harness.h"

using namespace DebugEngine;

namespace {
	const SymbolEntry* Find(const std::vector<SymbolEntry>& entries, const wchar_t* path) {
		auto it = std::find_if(entries.begin(), entries.end(), [&](auto& e) { return e.Path == path; });
		return it == entries.end() ? nullptr : &*it;
	}

	bool Contains(const SymbolEntry* e, const wchar_t* text) {
		return e && e->Value.find(text) != std::wstring::npos;
	}

	bool ExpressionIs(const SymbolEntry* e, const wchar_t* expression) {
		return e && e->Expression == expression;
	}
}

Task<void> ValueSuite(DebuggerX& dbg) {
	auto hr = co_await dbg.LaunchAndAttach(Target(L"values").c_str(), L"first second");
	Check(SUCCEEDED(hr), "launch values.exe");
	co_await dbg.ExecuteCommand(L"g `values.cpp:32`");

	std::vector<SymbolEntry> locals;
	hr = co_await dbg.GetLocals(locals, { L"point", L"shape", L"array", L"numbers", L"argv", L"shape\nNext", L"unique", L"text" });
	Dump("locals", locals);
	Check(SUCCEEDED(hr), "GetLocals");
	Check(Has(locals, L"argc") && Has(locals, L"argv") && Has(locals, L"numbers") && Has(locals, L"text") && Has(locals, L"shape"),
		"parameters and locals");
	Check(Contains(Find(locals, L"argc"), L"3"), "argc is 3");
	Check(Contains(Find(locals, L"point\nX"), L"10") && Contains(Find(locals, L"point\nY"), L"20"), "point's fields");
	Check(Contains(Find(locals, L"point"), L"X=10 Y=20"), "point's summary is {X=10 Y=20}");
	Check(Contains(Find(locals, L"shape\nName"), L"\"square\""), "a char* shows its string");
	Check(Contains(Find(locals, L"shape\nOrigin"), L"X=1"), "a nested structure's summary");
	Check(Contains(Find(locals, L"shape\nNext\nX"), L"10"), "a pointer to a structure expands to its fields");
	Check(Contains(Find(locals, L"array\n[1]"), L"8") && Contains(Find(locals, L"array"), L"7, 8, 9"), "an array's elements");
	Check(Contains(Find(locals, L"argv\n*argv"), L"values.exe"), "argv expands to *argv, a string");

	// The Kits engine has the standard library's visualizers (its Visualizers folder); System32's and the package's do
	// not: they show the fields
	const bool visualized = Contains(Find(locals, L"text"), L"\"hello\"");
	printf("(the engine %s the standard library's visualizers)\n", visualized ? "has" : "does not have");
	if (visualized) {
		Check(Contains(Find(locals, L"numbers"), L"size=0x3"), "a vector shows its size");
		Check(Contains(Find(locals, L"numbers\n[2]"), L"3"), "a vector's elements");
		Check(Find(locals, L"numbers\n[Raw View]") != nullptr, "a visualized value has a [Raw View]");
		Check(Find(locals, L"unique\n[ptr]") != nullptr, "unique_ptr's [ptr]");
	}
	else
		Check(Find(locals, L"numbers\n_Mypair") != nullptr, "without a visualizer: the fields");
	Check(Find(locals, L"text\nnpos") == nullptr && Find(locals, L"text\n[Raw View]\nnpos") == nullptr, "no static members");

	// expressions, and assigning to them
	Check(ExpressionIs(Find(locals, L"point\nX"), L"point.X"), "point.X has an expression");
	Check(ExpressionIs(Find(locals, L"array\n[1]"), L"array[1]"), "array[1] has an expression");
	Check(Find(locals, L"shape\nNext\nX") && !Find(locals, L"shape\nNext\nX")->Expression.empty(), "shape.Next->X has an expression");
	Check(Find(locals, L"point") && Find(locals, L"point")->Expression.empty(), "a structure is not assigned to");

	std::wstring error;
	g_Events.MemoryChanged = 0;
	hr = co_await dbg.AssignValue(L"point.X", L"42", error);
	Check(SUCCEEDED(hr), "assign point.X = 42");
	Check(g_Events.MemoryChanged == 1, "MemoryChanged after an assignment");
	Check(co_await Eval(dbg, L"@@c++(point.X)") == 42, "point.X is 42");
	hr = co_await dbg.AssignValue(L"array[1]", L"0x80", error);
	Check(SUCCEEDED(hr) && co_await Eval(dbg, L"@@c++(array[1])") == 0x80, "assign array[1] = 0x80");
	if (auto next = Find(locals, L"shape\nNext\nX")) {
		printf("(shape.Next->X: %S)\n", next->Expression.c_str());
		hr = co_await dbg.AssignValue(next->Expression.c_str(), L"point.Y + 1", error);
		Check(SUCCEEDED(hr) && co_await Eval(dbg, L"@@c++(point.X)") == 21, "assign through a pointer, from an expression");
	}
	hr = co_await dbg.AssignValue(L"letter", L"'B'", error);
	Check(SUCCEEDED(hr) && co_await Eval(dbg, L"@@c++(letter)") == 'B', "assign a character");
	hr = co_await dbg.AssignValue(L"point.X", L"no_such_thing_xyz", error);
	printf("(error: %S)\n", error.c_str());
	Check(FAILED(hr) && !error.empty(), "a bad value fails, and says why");

	locals.clear();
	co_await dbg.GetLocals(locals, { L"point" });
	Check(Contains(Find(locals, L"point\nX"), L"21"), "the locals show the new value");

	// watches
	std::vector<SymbolEntry> watches;
	hr = co_await dbg.EvaluateWatches(watches, { L"point.X * 2", L"no_such_thing_xyz", L"shape", L"text" }, { L"shape" });
	Dump("watches", watches);
	Check(std::count_if(watches.begin(), watches.end(), [](auto& e) { return e.Depth == 0; }) == 4, "one root per watch expression");
	Check(Contains(Find(watches, L"point.X * 2"), L"42") && !Find(watches, L"point.X * 2")->Error, "an arithmetic watch");
	Check(Find(watches, L"no_such_thing_xyz") && Find(watches, L"no_such_thing_xyz")->Error, "an unknown name is an error row");
	Check(ExpressionIs(Find(watches, L"shape\nScale"), L"shape.Scale"), "a watch's field has an expression");
	if (visualized)
		Check(Contains(Find(watches, L"text"), L"\"hello\""), "a watch of a string shows the string");

	co_await dbg.Stop();
	::PostQuitMessage(0);
}
