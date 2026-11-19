#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>
#include <DbgModel.h>
#include "Debugger.h"

namespace DebugEngine {
	//
	// Flattens values of the debugger's data model into SymbolEntry rows (the Locals and Watch panes), as dx shows them:
	// a type with a visualizer (NatVis: the standard library's, ...) by what it shows -- its keys ([size], ...) and its
	// elements --, with its fields under [Raw View]; any other structure by its fields (and base classes), an array and
	// whatever else can be iterated by its elements, a pointer by what it points to. Only what is expanded (by path) is
	// walked. Debug thread only.
	//
	// Some rows have a C++ expression for the value (Expression), which is what can be assigned to (AssignValue). It is
	// made from the parent's: a field is parent.field, an element parent[i], what a pointer points to *(parent); a key
	// of a visualizer ([size]) has none, and neither has anything under it.
	//
	class ValueWalker {
	public:
		ValueWalker(IDebugDataSpaces4* data, const std::set<std::wstring>& expanded, std::vector<SymbolEntry>& entries);

		// a row for the value (depth 0), and its children if it is expanded
		void Add(IModelObject* value, const std::wstring& name, std::optional<std::wstring> expression);
		// rows for the keys of a synthetic object (@$curframe.LocalVariables: the locals), at depth 0; their expressions
		// are their names
		void AddKeys(IModelObject* object);

		// the one line for a value that the Value column shows ("hello", { size=0x3 }, 0x1234 "text", {X=1 Y=2}, ...)
		std::wstring Format(IModelObject* value);

		// rows (and children) are not made for more elements than this; a last row says there are more
		static constexpr ULONG MaxElements = 1000;

	private:
		struct Child {
			std::wstring Name;
			CComPtr<IModelObject> Value;
			std::optional<std::wstring> Expression;
			bool RawView{ false };		// the [Raw View] row: Value's fields, though it has a visualizer
			bool More{ false };			// a row that says there are more elements
		};

		void AddRow(const Child& child, const std::wstring& parentPath, ULONG depth);
		std::vector<Child> GetChildren(IModelObject* value, const std::wstring& name, const std::optional<std::wstring>& expression,
			bool rawView, ULONG max = MaxElements);
		bool HasChildren(IModelObject* value, bool rawView);
		void AddFields(IModelObject* value, const std::optional<std::wstring>& expression, std::vector<Child>& children, ULONG max);
		std::wstring FormatPointer(IModelObject* value);
		std::wstring Summary(IModelObject* value);

		IDebugDataSpaces4* m_Data;
		const std::set<std::wstring>& m_Expanded;
		std::vector<SymbolEntry>& m_Entries;
	};

	// the type's name, empty if it has none
	std::wstring TypeName(IModelObject* value);
	// the text of a value the data model shows as it is (a number, an error's message, a visualizer's display string)
	std::wstring DisplayString(IModelObject* value);
}
