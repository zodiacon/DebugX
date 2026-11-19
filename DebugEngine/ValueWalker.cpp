#include "pch.h"
#include "ValueWalker.h"
#include <algorithm>
#include <format>

using namespace DebugEngine;

namespace {
	template<typename T>
	CComPtr<T> GetConcept(IModelObject* value) {
		CComPtr<IUnknown> unk;
		CComPtr<T> result;
		if (SUCCEEDED(value->GetConcept(__uuidof(T), &unk, nullptr)) && unk)
			unk.QueryInterface(&result);
		return result;
	}

	ModelObjectKind KindOf(IModelObject* value) {
		ModelObjectKind kind = ObjectNoValue;
		value->GetKind(&kind);
		return kind;
	}

	// the type, with typedefs looked through
	CComPtr<IDebugHostType> FinalType(IDebugHostType* type) {
		CComPtr<IDebugHostType> result(type);
		CComQIPtr<IDebugHostType2> type2(type);
		bool isTypedef = false;
		if (type2 && SUCCEEDED(type2->IsTypedef(&isTypedef)) && isTypedef) {
			CComPtr<IDebugHostType2> final;
			if (SUCCEEDED(type2->GetTypedefFinalBaseType(&final)) && final)
				result = final;
		}
		return result;
	}

	CComPtr<IDebugHostType> TypeOf(IModelObject* value) {
		CComPtr<IDebugHostType> type;
		if (FAILED(value->GetTypeInfo(&type)) || !type)
			return nullptr;
		return FinalType(type);
	}

	bool TypeKindOf(IDebugHostType* type, TypeKind& kind) {
		return type && SUCCEEDED(type->GetTypeKind(&kind));
	}

	bool IsPointer(IModelObject* value) {
		TypeKind kind;
		return KindOf(value) == ObjectIntrinsic && TypeKindOf(TypeOf(value), kind) && kind == TypePointer;
	}

	// the keys of the object that are not methods (Where, Select, ToDisplayString, ... are on everything)
	template<typename F>
	void ForEachKey(IModelObject* value, F f) {
		CComPtr<IKeyEnumerator> keys;
		if (FAILED(value->EnumerateKeyValues(&keys)) || !keys)
			return;
		while (true) {
			CComBSTR key;
			CComPtr<IModelObject> keyValue;
			CComPtr<IKeyStore> metadata;
			if (keys->GetNext(&key, &keyValue, &metadata) != S_OK)
				break;
			if (!keyValue || KindOf(keyValue) == ObjectMethod)
				continue;
			if (!f(std::wstring(key ? key.m_str : L""), keyValue.p))
				break;
		}
	}

	bool HasKeys(IModelObject* value) {
		bool any = false;
		ForEachKey(value, [&](const std::wstring&, IModelObject*) { any = true; return false; });
		return any;
	}

	// a visualizer (NatVis) is shown by its keys and elements; a structure without one by its fields. (An array, or a
	// pointer, is iterable too, but has no keys.)
	bool IsVisualized(IModelObject* value) {
		TypeKind kind;
		if (KindOf(value) != ObjectTargetObject || !TypeKindOf(TypeOf(value), kind) || kind != TypeUDT)
			return false;
		return HasKeys(value) || GetConcept<IIterableConcept>(value) != nullptr;
	}

	bool IsSimpleExpression(const std::wstring& expression) {
		return std::all_of(expression.begin(), expression.end(), [](wchar_t c) {
			return iswalnum(c) || c == L'_' || c == L'.' || c == L'[' || c == L']' || c == L':' || c == L'$' || c == L'@';
			});
	}

	std::wstring Parenthesized(const std::wstring& expression) {
		return IsSimpleExpression(expression) ? expression : L"(" + expression + L")";
	}

	bool IsIdentifier(const std::wstring& name) {
		return !name.empty() && !iswdigit(name[0]) && std::all_of(name.begin(), name.end(), [](wchar_t c) { return iswalnum(c) || c == L'_'; });
	}

	std::optional<std::wstring> Member(const std::optional<std::wstring>& expression, const std::wstring& name) {
		if (!expression || !IsIdentifier(name))
			return std::nullopt;
		return Parenthesized(*expression) + L"." + name;
	}
}

std::wstring DebugEngine::TypeName(IModelObject* value) {
	CComPtr<IDebugHostType> type;
	CComBSTR name;
	if (FAILED(value->GetTypeInfo(&type)) || !type || FAILED(type->GetName(&name)) || !name)
		return {};
	return name.m_str;
}

std::wstring DebugEngine::DisplayString(IModelObject* value) {
	if (auto display = GetConcept<IStringDisplayableConcept>(value)) {
		CComBSTR text;
		if (SUCCEEDED(display->ToDisplayString(value, nullptr, &text)))
			return text ? text.m_str : L"";
	}
	CComVariant v;
	if (SUCCEEDED(value->GetIntrinsicValue(&v)) && SUCCEEDED(v.ChangeType(VT_BSTR)) && v.bstrVal)
		return v.bstrVal;
	return {};
}

ValueWalker::ValueWalker(IDebugDataSpaces4* data, const std::set<std::wstring>& expanded, std::vector<SymbolEntry>& entries)
	: m_Data(data), m_Expanded(expanded), m_Entries(entries) {
}

void ValueWalker::Add(IModelObject* value, const std::wstring& name, std::optional<std::wstring> expression) {
	AddRow(Child{ name, value, std::move(expression) }, L"", 0);
}

void ValueWalker::AddKeys(IModelObject* object) {
	ForEachKey(object, [&](const std::wstring& key, IModelObject* value) {
		AddRow(Child{ key, value, key }, L"", 0);
		return true;
		});
}

void ValueWalker::AddRow(const Child& child, const std::wstring& parentPath, ULONG depth) {
	SymbolEntry entry;
	entry.Name = child.Name;
	entry.Path = parentPath.empty() && depth == 0 ? child.Name : parentPath + L'\n' + child.Name;
	entry.Depth = depth;
	if (child.More) {
		entry.Value = std::format(L"(only the first {} are shown)", MaxElements);
		m_Entries.push_back(std::move(entry));
		return;
	}
	auto value = child.Value.p;
	auto kind = KindOf(value);
	if (!child.RawView) {
		entry.Type = TypeName(value);
		entry.Error = kind == ObjectError;
		entry.Value = Format(value);
		if (kind == ObjectIntrinsic && child.Expression)
			entry.Expression = *child.Expression;
	}
	entry.Expandable = HasChildren(value, child.RawView);
	entry.Expanded = entry.Expandable && m_Expanded.contains(entry.Path);
	auto path = entry.Path;
	bool expanded = entry.Expanded;
	m_Entries.push_back(std::move(entry));
	if (!expanded)
		return;

	for (auto& c : GetChildren(value, child.Name, child.Expression, child.RawView))
		AddRow(c, path, depth + 1);
}

bool ValueWalker::HasChildren(IModelObject* value, bool rawView) {
	switch (KindOf(value)) {
		case ObjectIntrinsic: {
			if (!IsPointer(value))
				return false;
			// (not a null pointer, nor one to void or a function: Dereference fails for those)
			CComVariant v;
			if (FAILED(value->GetIntrinsicValueAs(VT_UI8, &v)) || v.ullVal == 0)
				return false;
			CComPtr<IModelObject> target;
			return SUCCEEDED(value->Dereference(&target)) && target;
		}
		case ObjectSynthetic:
			return HasKeys(value) || GetConcept<IIterableConcept>(value) != nullptr;
		case ObjectTargetObject:
			return !GetChildren(value, L"", std::nullopt, rawView, 1).empty();
		default:
			return false;
	}
}

std::vector<ValueWalker::Child> ValueWalker::GetChildren(IModelObject* value, const std::wstring& name,
	const std::optional<std::wstring>& expression, bool rawView, ULONG max) {
	std::vector<Child> children;
	auto kind = KindOf(value);

	if (kind == ObjectIntrinsic) {
		if (!IsPointer(value))
			return children;
		CComPtr<IModelObject> target;
		if (FAILED(value->Dereference(&target)) || !target)
			return children;
		std::optional<std::wstring> targetExpression;
		if (expression)
			targetExpression = L"*" + Parenthesized(*expression);
		// a pointer to a structure (or an array): its fields, as if they were the pointer's; to anything else: a row for it
		if (KindOf(target) == ObjectTargetObject)
			return GetChildren(target, name, targetExpression, false, max);
		children.push_back({ L"*" + name, target, targetExpression });
		return children;
	}

	if (kind != ObjectTargetObject && kind != ObjectSynthetic)
		return children;

	if (rawView) {
		AddFields(value, expression, children, max);
		return children;
	}

	const bool visualized = kind == ObjectTargetObject && IsVisualized(value);
	if (kind == ObjectSynthetic || visualized) {
		ForEachKey(value, [&](const std::wstring& key, IModelObject* keyValue) {
			children.push_back({ key, keyValue, Member(expression, key) });
			return children.size() < max;
			});
	}

	if (children.size() < max && (kind == ObjectSynthetic || visualized || !HasKeys(value))) {
		if (auto iterable = GetConcept<IIterableConcept>(value)) {
			ULONG64 dimensions = 0;
			iterable->GetDefaultIndexDimensionality(value, &dimensions);
			CComPtr<IModelIterator> iterator;
			if (SUCCEEDED(iterable->GetIterator(value, &iterator)) && iterator) {
				for (ULONG i = 0; ; i++) {
					CComPtr<IModelObject> element, indexer;
					CComPtr<IKeyStore> metadata;
					if (iterator->GetNext(&element, dimensions == 1 ? 1 : 0, dimensions == 1 ? &indexer.p : nullptr, &metadata) != S_OK || !element)
						break;
					if (i >= MaxElements) {
						children.push_back({ L"[...]", nullptr, std::nullopt, false, true });
						break;
					}
					std::wstring index = indexer ? DisplayString(indexer) : std::to_wstring(i);
					std::optional<std::wstring> elementExpression;
					if (expression && indexer)
						elementExpression = Parenthesized(*expression) + L"[" + index + L"]";
					children.push_back({ L"[" + index + L"]", element, elementExpression });
					if (children.size() >= max)
						break;
				}
			}
		}
	}

	if (visualized) {
		if (children.size() < max)
			children.push_back({ L"[Raw View]", value, expression, true });
	}
	else if (kind == ObjectTargetObject && children.size() < max)
		AddFields(value, expression, children, max);
	return children;
}

void ValueWalker::AddFields(IModelObject* value, const std::optional<std::wstring>& expression, std::vector<Child>& children, ULONG max) {
	TypeKind typeKind;
	auto type = TypeOf(value);
	if (!TypeKindOf(type, typeKind) || typeKind != TypeUDT)
		return;

	// base classes first, a row each (their fields are the object's own: the same expression reaches them)
	CComPtr<IRawEnumerator> raw;
	if (SUCCEEDED(value->EnumerateRawValues(SymbolBaseClass, RawSearchNoBases, &raw)) && raw) {
		while (children.size() < max) {
			CComBSTR name;
			SymbolKind kind;
			CComPtr<IModelObject> base;
			if (raw->GetNext(&name, &kind, &base) != S_OK)
				break;
			if (base)
				children.push_back({ name ? name.m_str : L"", base, expression });
		}
	}

	// fields; not the static ones, nor constants (npos, _BUF_SIZE, ...): they are not part of the object
	std::set<std::wstring> notMembers;
	CComPtr<IDebugHostSymbolEnumerator> symbols;
	if (SUCCEEDED(type->EnumerateChildren(SymbolField, nullptr, &symbols)) && symbols) {
		CComPtr<IDebugHostSymbol> symbol;
		while (symbols->GetNext(&symbol) == S_OK) {
			CComQIPtr<IDebugHostField> field(symbol);
			LocationKind location;
			CComBSTR name;
			if (field && SUCCEEDED(field->GetLocationKind(&location)) && location != LocationMember && SUCCEEDED(symbol->GetName(&name)) && name)
				notMembers.insert(name.m_str);
			symbol.Release();
		}
	}
	raw.Release();
	if (SUCCEEDED(value->EnumerateRawValues(SymbolField, RawSearchNoBases, &raw)) && raw) {
		while (children.size() < max) {
			CComBSTR name;
			SymbolKind kind;
			CComPtr<IModelObject> field;
			if (raw->GetNext(&name, &kind, &field) != S_OK)
				break;
			std::wstring fieldName = name ? name.m_str : L"";
			if (field && !notMembers.contains(fieldName))
				children.push_back({ fieldName, field, Member(expression, fieldName) });
		}
	}
}

std::wstring ValueWalker::Format(IModelObject* value) {
	switch (KindOf(value)) {
		case ObjectIntrinsic:
			if (IsPointer(value))
				return FormatPointer(value);
			return DisplayString(value);

		case ObjectTargetObject: {
			auto text = IsVisualized(value) ? DisplayString(value) : std::wstring();
			return text.empty() ? Summary(value) : text;
		}

		case ObjectNoValue:
			return {};

		default:		// an error (its message), a synthetic object, ...
			return DisplayString(value);
	}
}

std::wstring ValueWalker::FormatPointer(IModelObject* value) {
	CComVariant v;
	if (FAILED(value->GetIntrinsicValueAs(VT_UI8, &v)))
		return DisplayString(value);
	auto address = v.ullVal;
	auto text = std::format(L"0x{:X}", address);
	if (address == 0)
		return text;

	// a string: what it points to
	CComPtr<IDebugHostType> pointee;
	IntrinsicKind intrinsic;
	TypeKind kind;
	auto type = TypeOf(value);
	if (!type || FAILED(type->GetBaseType(&pointee)) || !pointee)
		return text;
	pointee = FinalType(pointee);
	if (!TypeKindOf(pointee, kind) || kind != TypeIntrinsic || FAILED(pointee->GetIntrinsicType(&intrinsic, nullptr)))
		return text;

	constexpr ULONG MaxChars = 256;
	std::wstring str;
	if (intrinsic == IntrinsicChar) {
		char buffer[MaxChars + 1]{};
		ULONG read = 0;
		m_Data->ReadVirtual(address, buffer, MaxChars, &read);
		if (read == 0)
			return text + L" <memory access error>";
		buffer[(std::min)(read, MaxChars)] = 0;
		auto len = static_cast<int>(strlen(buffer));
		str.resize(len);
		str.resize(::MultiByteToWideChar(CP_UTF8, 0, buffer, len, str.data(), len));
		if (len == static_cast<int>(MaxChars))
			str += L"...";
	}
	else if (intrinsic == IntrinsicWChar || intrinsic == IntrinsicChar16) {
		wchar_t buffer[MaxChars + 1]{};
		ULONG read = 0;
		m_Data->ReadVirtual(address, buffer, MaxChars * sizeof(wchar_t), &read);
		if (read == 0)
			return text + L" <memory access error>";
		buffer[(std::min<ULONG>)(read / sizeof(wchar_t), MaxChars)] = 0;
		str = buffer;
		if (str.size() == MaxChars)
			str += L"...";
	}
	else
		return text;
	return text + L" \"" + str + L"\"";
}

std::wstring ValueWalker::Summary(IModelObject* value) {
	// a structure: its first fields, an array: its first elements ({X=1 Y=2}, { 7, 8, 9 }), as far as they are numbers
	// and the like (anything else is {...})
	constexpr size_t MaxLength = 100;
	constexpr ULONG MaxItems = 10;
	TypeKind kind;
	auto type = TypeOf(value);
	if (!TypeKindOf(type, kind))
		return {};

	std::wstring text;
	auto children = GetChildren(value, L"", std::nullopt, false, MaxItems + 1);
	bool array = kind == TypeArray;
	for (size_t i = 0; i < children.size(); i++) {
		auto& c = children[i];
		if (c.More || c.RawView || i == MaxItems || text.size() > MaxLength) {
			text += L" ...";
			break;
		}
		std::wstring item;
		auto childKind = KindOf(c.Value);
		if (childKind == ObjectIntrinsic)
			item = IsPointer(c.Value) ? FormatPointer(c.Value) : DisplayString(c.Value);
		else if (childKind == ObjectTargetObject && IsVisualized(c.Value))
			item = DisplayString(c.Value);
		if (item.empty())
			item = L"{...}";
		if (!text.empty())
			text += array ? L", " : L" ";
		text += array ? item : c.Name + L"=" + item;
	}
	return array ? L"{ " + text + L" }" : L"{" + text + L"}";
}
