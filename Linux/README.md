# DebugX for Linux

DebugX's GUI over GDB, for user-mode programs. The same engine-neutral interface as the Windows build,
`../DebugEngine/IDebugger.h`, implemented by `GdbEngine::GdbDebugger` on GDB/MI, with a Qt 6 front end.

## Build

Needs a C++20 compiler with `<format>` (GCC 13+, Clang 17+), CMake 3.21+, Ninja, GDB, and Qt 6 (Widgets) and QScintilla
for the GUI:

```sh
sudo apt install g++ cmake ninja-build gdb qt6-base-dev libqscintilla2-qt6-dev
cmake --preset debug && cmake --build build/debug
build/debug/debugx [program [arguments...]]     # or -p <pid>, -pn <name>, -z <core>
build/debug/Tests/GdbEngineTests                # or: ctest --preset debug
```

In Qt Creator, open `CMakeLists.txt` (or this folder: the presets are found). Without Qt or QScintilla the engine and its tests
still build (`-DDEBUGX_BUILD_GUI=OFF` turns the GUI off explicitly).

`DEBUGX_GDB` picks another GDB. Attaching to a process DebugX did not start needs `kernel.yama.ptrace_scope` 0 (or root)
on distributions that set it to 1, as Ubuntu does.

## Layout

- `GdbEngine/` (static library): `GdbProcess` runs `gdb --interpreter=mi3` with pipes and gives the debugged programs a
  pseudo-terminal of their own; `MiParser` parses MI records; `GdbDebugger : IDebugger` sends token-numbered MI commands and
  turns `*stopped`, `=thread-created`, `=library-loaded`, `=breakpoint-modified`, ... into DebugX's notifications through the
  caller's dispatcher; `EventLoop` is a dispatcher for programs without a GUI toolkit (the tests).
- `Tests/`: `GdbEngineTests` (suites `launch bp step values crash threads break`, as `DebugEngineTests` on Windows) and the
  programs it debugs (`Tests/Targets`, `-O0 -g`, lines found by `// @marker`).
- `App/` (the `debugx` executable): `QtDispatcher`, `DebugSession` (the debugger and the shared `../DebugX/SessionModel`),
  `MainWindow`, the panes (`PaneBase`: refresh when dirty, visible and stopped), `CodeEditor` (QScintilla: source documents and
  Disassembly), dialogs.
- `Shared/SharedPch.h`: what DebugX's `pch.h` gives the Windows sources this build compiles (`SessionModel.cpp`).

## GDB, compared with DbgEng

- The initial break of a launch is at `main` (a temporary breakpoint DebugX hides), else at the first instruction.
- Breakpoints stay from one run to the next: GDB keeps them, and the Breakpoints pane shows them without a target. They
  are also saved for each program and set again when it next runs.
- Conditions, pass counts (GDB's ignore count) and thread-specific breakpoints are GDB's own.
- Locals and Watch use variable objects with the pretty-printers, so libstdc++'s containers show their elements.
- Exception filters are GDB's signal handling (`handle SIGx stop/print/pass`). Core dumps are written with `gcore` and opened.
- Handles are `/proc/<pid>/fd`; memory search is `find` over `/proc/<pid>/maps`' readable regions.
- Symbols for what has none (a distribution's programs and libraries) come from debuginfod servers, as Windows' come from
  a symbol server: on by default (Options > Symbol and Source Paths), the servers in `DEBUGINFOD_URLS`, else the system's
  (`/etc/debuginfod/*.urls`; the variable is set only in login shells). A download shows in the Command view and makes
  that start or stop wait. Installed debug packages (`libc6-dbg`, `*-dbgsym`) need no download.
- `DEBUGX_MI_LOG=<file>` logs every line to and from GDB, with times.

## Not yet

Qt-ADS instead of `QDockWidget`; the panes and dialogs Windows has that this does not yet:
Handles, Exception Settings, memory formats and search, thread freeze/suspend; `gdbserver` (the process server); the
Assistant and the MCP server.
