#pragma once

#include <vector>
#include "KernelAnalyzerBase.h"
#include "KernelGlobals.h"

namespace DebugX::Analysis {
	// an object in a directory of the object manager's namespace
	struct KernelObject {
		ULONG64 Address{ 0 };		// the object (its body)
		CString Name;
		CString Type;				// Directory, SymbolicLink, Device, Driver, Event, ...
		CString LinkTarget;			// a symbolic link's target
	};

	constexpr int MajorFunctionCount = 28;		// IRP_MJ_MAXIMUM_FUNCTION + 1

	struct KernelDriver {
		ULONG64 Address{ 0 };		// the DRIVER_OBJECT
		CString Name;				// \Driver\Null
		ULONG64 Start{ 0 };
		ULONG Size{ 0 };
		ULONG64 Init{ 0 };
		ULONG64 Unload{ 0 };
		ULONG64 FirstDevice{ 0 };
		ULONG DeviceCount{ 0 };
		ULONG64 MajorFunctions[MajorFunctionCount]{};
	};

	// an IRP a thread has issued that is not complete yet
	struct KernelIrp {
		ULONG64 Address{ 0 };
		ULONG64 Thread{ 0 };		// the ETHREAD
		UCHAR MajorFunction{ 0 }, MinorFunction{ 0 };	// of the current stack location: the driver that has it now
		ULONG64 Device{ 0 };
		ULONG64 Driver{ 0 };
		CString DriverName;
		ULONG64 FileObject{ 0 };
		CString FileName;
		ULONG Flags{ 0 };
		char CurrentLocation{ 0 }, StackCount{ 0 };
	};

	//
	// The object manager's namespace (\, \Driver, \Device, ...), the driver objects, and the IRPs of threads, from the
	// kernel's structures (public symbols have their types): an object's header is before its body, its type is
	// ObTypeIndexTable[TypeIndex ^ ObHeaderCookie ^ the header's address' second byte], its name is in the header's
	// name information (ObpInfoMaskToOffset says where), and a directory is 37 hash buckets of entries.
	//
	class KernelObjectAnalyzer : public KernelAnalyzerBase {
	public:
		explicit KernelObjectAnalyzer(IDebugClient* client);

		// the root directory (\); 0 if the symbols do not have it
		ULONG64 GetRootDirectory() const;
		// the object at a path ("\Driver", "\Device\Null"), from the root; 0 if there is none
		ULONG64 FindObject(PCWSTR path) const;
		std::vector<KernelObject> EnumDirectory(ULONG64 directory) const;
		CString GetObjectName(ULONG64 object) const;
		CString GetObjectTypeName(ULONG64 object) const;
		CString GetSymbolicLinkTarget(ULONG64 link) const;

		// the drivers: the objects in \Driver and \FileSystem
		std::vector<KernelDriver> EnumDrivers() const;
		KernelDriver GetDriver(ULONG64 driverObject) const;

		// the IRPs the thread (an ETHREAD) has issued that are not complete (its IrpList)
		std::vector<KernelIrp> EnumThreadIrps(ULONG64 thread) const;

		CString ReadUnicodeString(ULONG64 address) const;

	private:
		ULONG64 Header(ULONG64 object) const;

		KernelGlobals m_Globals;
	};

	// IRP_MJ_CREATE, ..., or a number
	CString MajorFunctionName(UCHAR major);
}
