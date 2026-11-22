#pragma once

#include <string>

//
// What DebugX is started with, as WinDbg takes it (so that it can stand in for it: in a script, an "Open with", or as
// the postmortem debugger of AeDebug, "DebugX.exe -p %ld -e %ld -g"):
//
//   DebugX.exe program.exe [arguments]    run it under the debugger
//   DebugX.exe file.dmp                   open the dump (also -z file.dmp)
//   -p <pid>                              attach to the process
//   -pn <name>                            attach to the one process of that name
//   -e <event>                            (with -p) signal the event once attached: AeDebug's
//   -g                                    go on past the initial break
//   -k <connection>                       a kernel over a connection ("net:port=50000,key=...")
//   -kl                                   the local kernel
//
struct CommandLine {
	enum class Kind {
		None,			// nothing to start
		Run, Attach, AttachByName, Dump, Kernel, LocalKernel,
		Help,			// -? or something it does not know: Error says what, Usage how
	};
	Kind Type{ Kind::None };
	std::wstring Path;			// the program, the dump, the process name, the kernel connection
	std::wstring Arguments;		// the program's
	DWORD Pid{ 0 };
	ULONG_PTR Event{ 0 };		// -e: a handle (inherited) to signal
	bool Go{ false };			// -g
	std::wstring Error;

	static CommandLine Parse(PCWSTR commandLine);
	static PCWSTR Usage();
};
