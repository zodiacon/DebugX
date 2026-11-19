#include "pch.h"
#include "KernelObjectAnalyzer.h"
#include <map>

using namespace DebugX::Analysis;

namespace {
	constexpr int DirectoryBuckets = 37;
	constexpr UCHAR NameInfoBit = 0x2, CreatorInfoBit = 0x1;

	struct UnicodeString64 {
		USHORT Length;
		USHORT MaximumLength;
		ULONG Padding;
		ULONG64 Buffer;
	};
}

KernelObjectAnalyzer::KernelObjectAnalyzer(IDebugClient* client) : KernelAnalyzerBase(client), m_Globals(CComQIPtr<IDebugSymbols>(client)) {
}

CString KernelObjectAnalyzer::ReadUnicodeString(ULONG64 address) const {
	HRESULT hr;
	auto us = ReadVirtual<UnicodeString64>(address, &hr);
	CString text;
	if (FAILED(hr) || us.Length == 0 || us.Buffer == 0)
		return text;
	auto length = (std::min<USHORT>)(us.Length, 2048) / sizeof(WCHAR);
	ULONG read = 0;
	Data()->ReadVirtual(us.Buffer, text.GetBufferSetLength(static_cast<int>(length)), static_cast<ULONG>(length * sizeof(WCHAR)), &read);
	text.ReleaseBufferSetLength(read / sizeof(WCHAR));
	return text;
}

ULONG64 KernelObjectAnalyzer::Header(ULONG64 object) const {
	auto body = GetFieldOffset(GetTypeId("_OBJECT_HEADER"), "Body");
	return body < 0 ? 0 : object - body;
}

CString KernelObjectAnalyzer::GetObjectName(ULONG64 object) const {
	auto header = Header(object);
	auto infoMaskOffset = GetFieldOffset(GetTypeId("_OBJECT_HEADER"), "InfoMask");
	auto table = m_Globals.GetVariable("ObpInfoMaskToOffset");
	if (!header || infoMaskOffset < 0 || !table)
		return {};
	auto mask = ReadVirtual<UCHAR>(header + infoMaskOffset);
	if (!(mask & NameInfoBit))
		return {};
	// (the name information is before the header, after whatever comes before it: the creator information)
	auto offset = ReadVirtual<UCHAR>(table + (mask & (NameInfoBit | CreatorInfoBit)));
	auto name = GetFieldOffset(GetTypeId("_OBJECT_HEADER_NAME_INFO"), "Name");
	return name < 0 ? CString() : ReadUnicodeString(header - offset + name);
}

CString KernelObjectAnalyzer::GetObjectTypeName(ULONG64 object) const {
	auto header = Header(object);
	auto typeIndexOffset = GetFieldOffset(GetTypeId("_OBJECT_HEADER"), "TypeIndex");
	auto table = m_Globals.GetVariable("ObTypeIndexTable");
	if (!header || typeIndexOffset < 0 || !table)
		return {};
	// (Windows 10 on: the index is scrambled with a cookie and the header's address; ObHeaderCookie is not there before)
	UCHAR index = ReadVirtual<UCHAR>(header + typeIndexOffset);
	if (auto cookie = m_Globals.GetVariable("ObHeaderCookie"))
		index = index ^ ReadVirtual<UCHAR>(cookie) ^ static_cast<UCHAR>(header >> 8);

	// (the same few types over and over: by the table's entry)
	static std::map<std::pair<ULONG64, UCHAR>, CString> names;
	auto key = std::make_pair(table, index);
	if (auto it = names.find(key); it != names.end())
		return it->second;
	auto type = ReadVirtual<ULONG64>(table + index * sizeof(ULONG64));
	auto nameOffset = GetFieldOffset(GetTypeId("_OBJECT_TYPE"), "Name");
	CString name = type && nameOffset >= 0 ? ReadUnicodeString(type + nameOffset) : CString();
	if (!name.IsEmpty())
		names[key] = name;
	return name;
}

CString KernelObjectAnalyzer::GetSymbolicLinkTarget(ULONG64 link) const {
	auto offset = GetFieldOffset(GetTypeId("_OBJECT_SYMBOLIC_LINK"), "LinkTarget");
	return offset < 0 ? CString() : ReadUnicodeString(link + offset);
}

ULONG64 KernelObjectAnalyzer::GetRootDirectory() const {
	auto root = m_Globals.GetVariable("ObpRootDirectoryObject");
	return root ? ReadVirtual<ULONG64>(root) : 0;
}

std::vector<KernelObject> KernelObjectAnalyzer::EnumDirectory(ULONG64 directory) const {
	std::vector<KernelObject> objects;
	auto buckets = GetFieldOffset(GetTypeId("_OBJECT_DIRECTORY"), "HashBuckets");
	auto entryType = GetTypeId("_OBJECT_DIRECTORY_ENTRY");
	auto chain = GetFieldOffset(entryType, "ChainLink"), objectOffset = GetFieldOffset(entryType, "Object");
	if (!directory || buckets < 0 || chain < 0 || objectOffset < 0)
		return objects;

	ULONG64 heads[DirectoryBuckets]{};
	if (FAILED(Data()->ReadVirtual(directory + buckets, heads, sizeof(heads), nullptr)))
		return objects;
	for (auto entry : heads) {
		// (a bound on a chain: memory that changes under the walk could make a loop)
		for (int count = 0; entry && count < 10000; count++) {
			KernelObject object;
			object.Address = ReadVirtual<ULONG64>(entry + objectOffset);
			if (object.Address) {
				object.Name = GetObjectName(object.Address);
				object.Type = GetObjectTypeName(object.Address);
				if (object.Type == L"SymbolicLink")
					object.LinkTarget = GetSymbolicLinkTarget(object.Address);
				objects.push_back(std::move(object));
			}
			entry = ReadVirtual<ULONG64>(entry + chain);
		}
	}
	return objects;
}

ULONG64 KernelObjectAnalyzer::FindObject(PCWSTR path) const {
	auto object = GetRootDirectory();
	CString rest(path);
	rest.Trim(L"\\");
	int pos = 0;
	for (auto part = rest.Tokenize(L"\\", pos); !part.IsEmpty() && object; part = rest.Tokenize(L"\\", pos)) {
		ULONG64 found = 0;
		for (auto& o : EnumDirectory(object)) {
			if (o.Name.CompareNoCase(part) == 0) {
				found = o.Address;
				break;
			}
		}
		object = found;
	}
	return object;
}

KernelDriver KernelObjectAnalyzer::GetDriver(ULONG64 driverObject) const {
	KernelDriver driver;
	driver.Address = driverObject;
	auto type = GetTypeId("_DRIVER_OBJECT");
	auto field = [&](PCSTR name) { return GetFieldOffset(type, name); };
	if (auto offset = field("DriverName"); offset >= 0)
		driver.Name = ReadUnicodeString(driverObject + offset);
	if (auto offset = field("DriverStart"); offset >= 0)
		driver.Start = ReadVirtual<ULONG64>(driverObject + offset);
	if (auto offset = field("DriverSize"); offset >= 0)
		driver.Size = ReadVirtual<ULONG>(driverObject + offset);
	if (auto offset = field("DriverInit"); offset >= 0)
		driver.Init = ReadVirtual<ULONG64>(driverObject + offset);
	if (auto offset = field("DriverUnload"); offset >= 0)
		driver.Unload = ReadVirtual<ULONG64>(driverObject + offset);
	if (auto offset = field("MajorFunction"); offset >= 0)
		Data()->ReadVirtual(driverObject + offset, driver.MajorFunctions, sizeof(driver.MajorFunctions), nullptr);
	if (auto offset = field("DeviceObject"); offset >= 0) {
		driver.FirstDevice = ReadVirtual<ULONG64>(driverObject + offset);
		auto next = GetFieldOffset(GetTypeId("_DEVICE_OBJECT"), "NextDevice");
		for (auto device = driver.FirstDevice; device && next >= 0 && driver.DeviceCount < 10000; device = ReadVirtual<ULONG64>(device + next))
			driver.DeviceCount++;
	}
	return driver;
}

std::vector<KernelDriver> KernelObjectAnalyzer::EnumDrivers() const {
	std::vector<KernelDriver> drivers;
	for (auto directory : { L"\\Driver", L"\\FileSystem" }) {
		for (auto& object : EnumDirectory(FindObject(directory))) {
			if (object.Type == L"Driver") {
				auto driver = GetDriver(object.Address);
				if (driver.Name.IsEmpty())
					driver.Name = CString(directory) + L"\\" + object.Name;
				drivers.push_back(std::move(driver));
			}
		}
	}
	return drivers;
}

std::vector<KernelIrp> KernelObjectAnalyzer::EnumThreadIrps(ULONG64 thread) const {
	std::vector<KernelIrp> irps;
	auto irpList = GetFieldOffset(GetTypeId("_ETHREAD"), "IrpList");
	auto irpType = GetTypeId("_IRP");
	auto threadListEntry = GetFieldOffset(irpType, "ThreadListEntry");
	auto stack = GetFieldOffset(irpType, "Tail.Overlay.CurrentStackLocation");
	auto flags = GetFieldOffset(irpType, "Flags");
	auto current = GetFieldOffset(irpType, "CurrentLocation"), count = GetFieldOffset(irpType, "StackCount");
	auto stackType = GetTypeId("_IO_STACK_LOCATION");
	auto major = GetFieldOffset(stackType, "MajorFunction"), minor = GetFieldOffset(stackType, "MinorFunction");
	auto device = GetFieldOffset(stackType, "DeviceObject"), file = GetFieldOffset(stackType, "FileObject");
	auto driverOfDevice = GetFieldOffset(GetTypeId("_DEVICE_OBJECT"), "DriverObject");
	auto driverName = GetFieldOffset(GetTypeId("_DRIVER_OBJECT"), "DriverName");
	auto fileName = GetFieldOffset(GetTypeId("_FILE_OBJECT"), "FileName");
	if (irpList < 0 || threadListEntry < 0)
		return irps;

	auto head = thread + irpList;
	auto link = ReadVirtual<ULONG64>(head);
	for (int n = 0; link && link != head && n < 1000; n++, link = ReadVirtual<ULONG64>(link)) {
		KernelIrp irp;
		irp.Address = link - threadListEntry;
		irp.Thread = thread;
		if (flags >= 0)
			irp.Flags = ReadVirtual<ULONG>(irp.Address + flags);
		if (current >= 0)
			irp.CurrentLocation = ReadVirtual<char>(irp.Address + current);
		if (count >= 0)
			irp.StackCount = ReadVirtual<char>(irp.Address + count);
		// (the stack location of the driver that has it now; one past the last when it is being completed)
		if (stack >= 0 && irp.CurrentLocation >= 1 && irp.CurrentLocation <= irp.StackCount) {
			if (auto location = ReadVirtual<ULONG64>(irp.Address + stack)) {
				if (major >= 0)
					irp.MajorFunction = ReadVirtual<UCHAR>(location + major);
				if (minor >= 0)
					irp.MinorFunction = ReadVirtual<UCHAR>(location + minor);
				if (device >= 0)
					irp.Device = ReadVirtual<ULONG64>(location + device);
				if (file >= 0)
					irp.FileObject = ReadVirtual<ULONG64>(location + file);
			}
		}
		if (irp.Device && driverOfDevice >= 0) {
			irp.Driver = ReadVirtual<ULONG64>(irp.Device + driverOfDevice);
			if (irp.Driver && driverName >= 0)
				irp.DriverName = ReadUnicodeString(irp.Driver + driverName);
		}
		if (irp.FileObject && fileName >= 0)
			irp.FileName = ReadUnicodeString(irp.FileObject + fileName);
		irps.push_back(std::move(irp));
	}
	return irps;
}

CString DebugX::Analysis::MajorFunctionName(UCHAR major) {
	static const PCWSTR names[] = {
		L"CREATE", L"CREATE_NAMED_PIPE", L"CLOSE", L"READ", L"WRITE", L"QUERY_INFORMATION", L"SET_INFORMATION", L"QUERY_EA",
		L"SET_EA", L"FLUSH_BUFFERS", L"QUERY_VOLUME_INFORMATION", L"SET_VOLUME_INFORMATION", L"DIRECTORY_CONTROL",
		L"FILE_SYSTEM_CONTROL", L"DEVICE_CONTROL", L"INTERNAL_DEVICE_CONTROL", L"SHUTDOWN", L"LOCK_CONTROL", L"CLEANUP",
		L"CREATE_MAILSLOT", L"QUERY_SECURITY", L"SET_SECURITY", L"POWER", L"SYSTEM_CONTROL", L"DEVICE_CHANGE", L"QUERY_QUOTA",
		L"SET_QUOTA", L"PNP"
	};
	CString name;
	if (major < _countof(names))
		name.Format(L"IRP_MJ_%s", names[major]);
	else
		name.Format(L"0x%02X", major);
	return name;
}
