#include "pch.h"
#include "KernelProcessAnalyzer.h"
#include "KernelThreadAnalyzer.h"
#include "KernelProcessThreadEnumerator.h"
#include <winternl.h>
#include <string>
#include <unordered_map>

using namespace DebugX::Analysis;

static ULONG ActiveProcessLinksOffset;
static ULONG EProcessTypeId, KProcessTypeId;

typedef struct _OBJECT_NAME_INFORMATION {
	UNICODE_STRING Name;
} OBJECT_NAME_INFORMATION, * POBJECT_NAME_INFORMATION;

struct SE_AUDIT_PROCESS_CREATION_INFO {
	OBJECT_NAME_INFORMATION* ImageFileName;
};

KernelProcessAnalyzer::KernelProcessAnalyzer(IDebugClient* pClient) : 
	KernelAnalyzerBase(pClient), m_Globals(CComQIPtr<IDebugSymbols>(pClient)) {
	if (EProcessTypeId == 0) {
		auto hr = Symbols()->GetTypeId(KernelBase(), "_EPROCESS", &EProcessTypeId);
		ATLASSERT(SUCCEEDED(hr));
		hr = Symbols()->GetFieldOffset(KernelBase(), EProcessTypeId, "ActiveProcessLinks", &ActiveProcessLinksOffset);
		ATLASSERT(SUCCEEDED(hr));
		hr = Symbols()->GetTypeId(KernelBase(), "_KPROCESS", &KProcessTypeId);
	}
}

DebugX::Analysis::KernelProcessAnalyzer::~KernelProcessAnalyzer() = default;

bool KernelProcessAnalyzer::SetProcess(ULONG64 processAddress) {
	m_ProcessAddress = processAddress;
	m_ProcessEntry = processAddress + ActiveProcessLinksOffset;

	return true;
}

bool KernelProcessAnalyzer::SetProcessEntry(ULONG64 processEntry) {
	return SetProcess(processEntry - ActiveProcessLinksOffset);
}

ULONG DebugX::Analysis::KernelProcessAnalyzer::GetThreadCount() const {
	ULONG count = 0;
	Data()->ReadVirtual(m_ProcessAddress + GetFieldOffset(EProcessTypeId, "ActiveThreads"), &count, sizeof(count), nullptr);
	return count;
}

ULONG DebugX::Analysis::KernelProcessAnalyzer::GetSessionId() const {
	auto offset = GetFieldOffset(EProcessTypeId, "Session");
	auto sessionSpace = ReadVirtual<ULONG64>(m_ProcessAddress + offset);
	auto offsetSessionId = GetFieldOffset(GetTypeId("_MM_SESSION_SPACE"), "SessionId");
	return ReadVirtual<ULONG>(sessionSpace + offsetSessionId);
}

ULONG64 DebugX::Analysis::KernelProcessAnalyzer::GetDirectoryTableBase() const {
	return ReadVirtual<ULONG64>(m_ProcessAddress + GetFieldOffset(KProcessTypeId, "DirectoryTableBase"));
}

ULONG64 DebugX::Analysis::KernelProcessAnalyzer::GetPeb() const {
	return ReadVirtual<ULONG64>(m_ProcessAddress + GetFieldOffset(EProcessTypeId, "Peb"));
}

ULONG64 DebugX::Analysis::KernelProcessAnalyzer::GetHandleTable() const {
	return (ULONG64)ReadVirtual<PVOID>(m_ProcessAddress + GetFieldOffset(EProcessTypeId, "ObjectTable"));
}

ULONG DebugX::Analysis::KernelProcessAnalyzer::GetHandleCount() const {
	auto table = GetHandleTable();
	if (table == 0)
		return 0;

	auto nextHandleNeedingPool = ReadVirtual<ULONG>(table + GetFieldOffset(GetTypeId("_HANDLE_TABLE"), "NextHandleNeedingPool"));
	auto maxHandles = nextHandleNeedingPool / 4;
	const auto count = (1 << 12) / (ULONG)m_Globals.GetVariable("KeMaximumProcessors");
	maxHandles = maxHandles * (count - 1) / count;
	// incomplete
	return maxHandles;
}

ULONG64 DebugX::Analysis::KernelProcessAnalyzer::GetJob() const {
	return (ULONG64)ReadVirtual<PVOID>(m_ProcessAddress + GetFieldOffset(EProcessTypeId, "Job"));
}

ULONG DebugX::Analysis::KernelProcessAnalyzer::GetProcessId() const {
	PVOID data = nullptr;
	Data()->ReadVirtual(m_ProcessAddress + GetFieldOffset(EProcessTypeId, "UniqueProcessId"), &data, sizeof(data), nullptr);
	return PtrToUlong(data);
}

CString DebugX::Analysis::KernelProcessAnalyzer::GetImageName() const {
	auto name = GetFullImageName();
	auto index = name.ReverseFind(L'\\');
	return index < 0 ? name : name.Mid(index + 1);
}

CString KernelProcessAnalyzer::GetFullImageName() const {
	CString name;
	SE_AUDIT_PROCESS_CREATION_INFO apci;
	auto offset = GetFieldOffset(EProcessTypeId, "SeAuditProcessCreationInfo");
	Data()->ReadVirtual(m_ProcessAddress + offset, &apci, sizeof(apci), nullptr);
	auto info = &apci;
	auto nameInfo = info->ImageFileName;
	OBJECT_NAME_INFORMATION nameInfoLocal;
	Data()->ReadVirtual((ULONG64)nameInfo, &nameInfoLocal, sizeof(nameInfoLocal), nullptr);
	if (nameInfoLocal.Name.Length) {
		ATLASSERT(nameInfoLocal.Name.Buffer);
		Data()->ReadVirtual((ULONG64)nameInfoLocal.Name.Buffer,
			name.GetBufferSetLength(nameInfoLocal.Name.Length / sizeof(WCHAR)),
			nameInfoLocal.Name.Length, nullptr);
	}
	if (name.IsEmpty()) {
		CStringA namea;
		Data()->ReadVirtual(m_ProcessAddress + GetFieldOffset(EProcessTypeId, "ImageFileName"), namea.GetBufferSetLength(15), 15, nullptr);
		name = namea.TrimRight();
	}
	return name;
}

LARGE_INTEGER KernelProcessAnalyzer::GetCreateTime() const {
	return ReadVirtual<LARGE_INTEGER>(m_ProcessAddress + GetFieldOffset(EProcessTypeId, "CreateTime"));
}

LARGE_INTEGER KernelProcessAnalyzer::GetExitTime() const {
	return ReadVirtual<LARGE_INTEGER>(m_ProcessAddress + GetFieldOffset(EProcessTypeId, "ExitTime"));
}

ULONG64 KernelProcessAnalyzer::GetKernelTime() const {
	auto time = ReadVirtual<ULONG>(m_ProcessAddress + GetFieldOffset(KProcessTypeId, "KernelTime"));
	KernelProcessThreadEnumerator tenum(m_ProcessAddress, CComQIPtr<IDebugClient>(Client()));
	KernelThreadAnalyzer* ta;

	while (ta = tenum.GetNextThread()) {
		time += ta->GetKernelTime();
	}
	return time * (ULONG64)ReadVirtual<ULONG>(m_Globals.GetVariable("KeMaximumIncrement"));
}

ULONG64 KernelProcessAnalyzer::GetUserTime() const {
	auto time = ReadVirtual<ULONG>(m_ProcessAddress + GetFieldOffset(KProcessTypeId, "UserTime"));
	return time * (ULONG64)ReadVirtual<ULONG>(m_Globals.GetVariable("KeMaximumIncrement"));
}

ULONG64 KernelProcessAnalyzer::GetProcessAddress() const {
	return m_ProcessAddress;
}

ULONG64 DebugX::Analysis::KernelProcessAnalyzer::GetNextProcessLink() const {
	LIST_ENTRY entry;
	auto hr = Data()->ReadVirtual(m_ProcessAddress + ActiveProcessLinksOffset, &entry, sizeof(entry), nullptr);
	ATLASSERT(SUCCEEDED(hr));
	return (ULONG64)entry.Flink;
}

ULONG64 KernelProcessAnalyzer::MoveToNextProcess() {
	auto link = GetNextProcessLink();
	SetProcessEntry(link);
	return GetProcessAddress();
}

std::unique_ptr<KernelProcessThreadEnumerator> DebugX::Analysis::KernelProcessAnalyzer::GetThreadEnumerator() const {
	return std::make_unique<KernelProcessThreadEnumerator>(m_ProcessAddress, CComQIPtr<IDebugClient>(Client()));
}

