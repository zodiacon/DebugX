#include "pch.h"
#include "KernelThreadAnalyzer.h"

using namespace DebugX::Analysis;

const PCSTR EThreadName = "_ETHREAD";
const PCSTR KThreadName = "_KTHREAD";

KernelThreadAnalyzer::KernelThreadAnalyzer(ULONG64 address, IDebugClient* client) : 
	KernelAnalyzerBase(client), m_Address(address) {
	ATLTRACE(L"KernelThreadAnalyzer thread: 0x%llX\n", address);
}

LARGE_INTEGER KernelThreadAnalyzer::GetCreateTime() const {
	return ReadVirtual<LARGE_INTEGER>(m_Address + GetFieldOffset(GetTypeId(EThreadName), "CreateTime"));
}

LARGE_INTEGER KernelThreadAnalyzer::GetExitTime() const {
	return ReadVirtual<LARGE_INTEGER>(m_Address + GetFieldOffset(GetTypeId(EThreadName), "ExitTime"));
}

ClientId KernelThreadAnalyzer::GetClientId() const {
	HANDLE id[2] = { 0 };
	auto hr = Data()->ReadVirtual(m_Address + GetFieldOffset(GetTypeId(EThreadName), "Cid"), id, sizeof(id), nullptr);
	ATLASSERT(SUCCEEDED(hr));
	ClientId cid;
	cid.ProcessId = HandleToUlong(id[0]);
	cid.ThreadId = HandleToUlong(id[1]);
	return cid;
}

ULONG64 KernelThreadAnalyzer::GetNextThread() const {
	auto offset = GetFieldOffset(GetTypeId(EThreadName), "ThreadListEntry");
	return GetNextThreadLink() - offset;
}

ULONG64 KernelThreadAnalyzer::GetNextThreadLink() const {
	auto offset = GetFieldOffset(GetTypeId(EThreadName), "ThreadListEntry");
	auto entry = ReadVirtual<LIST_ENTRY>(m_Address + offset);
	auto nextEntry = (ULONG64)entry.Flink;
	ATLTRACE(L"KernelThreadAnalyzer next thread link: 0x%llX\n", nextEntry);
	return nextEntry;
}

ULONG KernelThreadAnalyzer::GetKernelTime() const {
	return ReadVirtual<ULONG>(m_Address + GetFieldOffset(GetTypeId("_KTHREAD"), "KernelTime"));
}

ULONG KernelThreadAnalyzer::GetUserTime() const {
	return ReadVirtual<ULONG>(m_Address + GetFieldOffset(GetTypeId("_KTHREAD"), "UserTime"));
}

ULONG64 KernelThreadAnalyzer::GetStartAddress() const {
	auto type = GetTypeId(EThreadName);
	auto start = ReadVirtual<ULONG64>(m_Address + GetFieldOffset(type, "Win32StartAddress"));
	return start ? start : ReadVirtual<ULONG64>(m_Address + GetFieldOffset(type, "StartAddress"));
}

UCHAR KernelThreadAnalyzer::GetState() const {
	return ReadVirtual<UCHAR>(m_Address + GetFieldOffset(GetTypeId(KThreadName), "State"));
}

ULONG64 KernelThreadAnalyzer::MoveToNextThread() {
	return m_Address = GetNextThread();
}
