#include "pch.h"
#include "EProcessAnalyzer.h"
#include <winternl.h>

using namespace DebugX::Analysis;

static ULONG ActiveProcessLinksOffset;
static ULONG EProcessTypeId;

typedef struct _OBJECT_NAME_INFORMATION {
	UNICODE_STRING Name;
} OBJECT_NAME_INFORMATION, * POBJECT_NAME_INFORMATION;

struct SE_AUDIT_PROCESS_CREATION_INFO {
	OBJECT_NAME_INFORMATION* ImageFileName;
};

EProcessAnalyzer::EProcessAnalyzer(ULONG64 nt, IDebugClient* pClient) : _ntAddress(nt) {
	_symbols = pClient;
	_data = pClient;

	if (EProcessTypeId == 0) {
		auto hr = _symbols->GetTypeId(nt, "_EPROCESS", &EProcessTypeId);
		ATLASSERT(SUCCEEDED(hr));
		hr = _symbols->GetFieldOffset(nt, EProcessTypeId, "ActiveProcessLinks", &ActiveProcessLinksOffset);
		ATLASSERT(SUCCEEDED(hr));
	}
}

bool EProcessAnalyzer::SetProcess(ULONG64 processAddress) {
	_processAddress = processAddress;
	_processEntry = processAddress + ActiveProcessLinksOffset;

	return SUCCEEDED(_data->ReadVirtual(_processAddress, _buffer, sizeof(_buffer), nullptr));
}

bool EProcessAnalyzer::SetProcessEntry(ULONG64 processEntry) {
	return SetProcess(processEntry - ActiveProcessLinksOffset);
}

const void* EProcessAnalyzer::GetData(ULONG offset) const {
	return _buffer + offset;
}

const void* EProcessAnalyzer::GetData(PCSTR field) const {
	ULONG offset;
	auto hr = _symbols->GetFieldOffset(_ntAddress, EProcessTypeId, field, &offset);
	return FAILED(hr) ? nullptr : _buffer + offset;
}

ULONG DebugX::Analysis::EProcessAnalyzer::GetProcessId() const {
	auto data = GetData("UniqueProcessId");
	return data ? PtrToUlong(*(PVOID**)data) : 0;
}

CString DebugX::Analysis::EProcessAnalyzer::GetImageName() const {
	auto name = GetFullImageName();
	auto index = name.ReverseFind(L'\\');
	return index < 0 ? name : name.Mid(index + 1);
}

CString EProcessAnalyzer::GetFullImageName() const {
	CString name;
	auto p = GetData("SeAuditProcessCreationInfo");
	if (p) {
		auto info = (SE_AUDIT_PROCESS_CREATION_INFO*)p;
		auto nameInfo = info->ImageFileName;
		BYTE local[1 << 10];
		_data->ReadVirtual((ULONG64)nameInfo, local, sizeof(local), nullptr);
		auto nameInfoLocal = (OBJECT_NAME_INFORMATION*)local;
		if (nameInfoLocal->Name.Length) {
			ATLASSERT(nameInfoLocal->Name.Buffer);
			_data->ReadVirtual((ULONG64)nameInfoLocal->Name.Buffer, 
				name.GetBufferSetLength(nameInfoLocal->Name.Length / sizeof(WCHAR)), 
				nameInfoLocal->Name.Length, nullptr);
		}
	}
	if (name.IsEmpty()) {
		p = GetData("ImageFileName");
		ATLASSERT(p);
		CStringA namea((PCSTR)p, 15);
		name = namea.TrimRight();
	}
	return name;
}

ULONG64 EProcessAnalyzer::GetProcessAddress() const {
	return _processAddress;
}

ULONG64 EProcessAnalyzer::MoveToNextProcess() {
	auto entry = (LIST_ENTRY*)(_buffer + ActiveProcessLinksOffset);
	SetProcessEntry((ULONG64)entry->Flink);
	return GetProcessAddress();
}