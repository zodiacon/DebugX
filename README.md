# DebugX

DebugX is a debugger for Windows with a graphical front end, built on the Windows Debug Engine (`dbgeng.dll`, the
engine behind WinDbg). It can:

- launch an executable under the debugger or attach to a running process (also through a process server on another
  machine)
- open crash dumps
- debug the local kernel, or a kernel over a connection

The UI is a docking layout, similar to Visual Studio's. Besides the Command window, it has panes for:

- source files, disassembly, call stack, registers
- locals and watch expressions (shown by the debugger data model, NatVis included), memory (editable), breakpoints
- threads, processes, modules and handles
- in kernel mode: processes, threads, drivers, the object namespace and IRPs

It has a default window layout for user mode and one for kernel mode, and comes in light and dark themes.

DebugX uses the Windows Kits debugger's engine (from the Debugging Tools for Windows) if it is installed, and
System32's otherwise. Install the Debugging Tools to get symbol servers and the debugger extensions (`!analyze`,
`!process`, ...): System32's engine has neither.

## Building

Requirements:

- Windows 10 or later
- Visual Studio 2026 (platform toolset v145) with the **Desktop development with C++** workload and a Windows 10/11 SDK
- [vcpkg](https://github.com/microsoft/vcpkg), with its MSBuild integration on (`vcpkg integrate install`)
- Optional: the Debugging Tools for Windows (from the Windows SDK installer), for the full engine described above

Steps:

1. Clone the repository with its submodules:

   ```
   git clone --recurse-submodules https://github.com/zodiacon/DebugX.git
   ```

   In an existing clone, run `git submodule update --init wtlhelper` instead. The `wtlhelper` submodule has WTL, the
   docking framework, the hex control and the UI helper library.

2. Install the vcpkg packages, as static libraries, for each platform you build:

   ```
   vcpkg install detours wil scintilla lexilla --triplet x64-windows-static
   vcpkg install detours wil scintilla lexilla --triplet x86-windows-static
   ```

3. Open `DebugX.sln` in Visual Studio and build. NuGet restores the other packages (the DbgEng headers and libraries,
   WIL). To build from the command line instead:

   ```
   msbuild DebugX.sln -t:Restore -p:RestorePackagesConfig=true
   msbuild DebugX.sln -p:Configuration=Release -p:Platform=x64
   ```

   The program is `x64\Release\DebugX.exe` (or `Release\DebugX.exe` for x86). Debug and Release build for x64 and x86.

### Tests

`Tests\DebugEngineTests` (x64) tests the engine wrapper against small test programs that are built with the solution.
Run `x64\Debug\DebugEngineTests.exe` after a Debug build. It runs every suite with each `dbgeng.dll` it can find, and
its exit code is the number of failed checks. Some suites say SKIP when what they need is not there. The kernel suite,
for example, needs an elevated prompt, `bcdedit /debug on` and the kernel's symbols.
