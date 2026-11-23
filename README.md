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

You can ask questions about the target in plain language, for example: "which threads are waiting in mymodule, and on
what?" Ask in DebugX's own Assistant pane (Claude, with your Anthropic API key), or from an AI client such as Claude Code
through DebugX's MCP server (see below).

DebugX uses the Windows Kits debugger's engine (from the Debugging Tools for Windows) if it is installed, and
System32's otherwise. Install the Debugging Tools to get symbol servers and the debugger extensions (`!analyze`,
`!process`, ...): System32's engine has neither.

## AI assistance

### The Assistant pane

**View > Assistant** opens a conversation with Claude about the target. Enter sends a message, Shift+Enter starts a new
line, Stop (or Esc) ends a turn, and New starts a new conversation. The first time, **Settings** asks for an Anthropic API
key from console.anthropic.com. DebugX keeps the key in Windows Credential Manager, or uses `ANTHROPIC_API_KEY` if that is
set. Settings also chooses the model (Claude Opus 5.5 by default) and its effort.

The assistant looks into the target with the tools listed below. The answer streams into the pane, with dimmed summaries
of the model's reasoning, each tool call, and the start of each result. Symbols and addresses are links to the Disassembly
pane. Before running, stepping, setting breakpoints or running a debugger command, the assistant asks, unless Settings
allows it. Your questions, and what the tools read of the target, are sent to Anthropic's API.

### MCP server

Turn it on in **Options > MCP Server**. DebugX then listens on `http://127.0.0.1:47011/mcp` (the port can be changed)
for MCP's Streamable HTTP transport. Each request must carry the bearer token the dialog shows. The dialog has the
command that adds DebugX to Claude Code:

```
claude mcp add --transport http --scope user debugx http://127.0.0.1:47011/mcp --header "Authorization: Bearer <token>"
```

The client then has tools for what is being debugged:

- the session, threads, and one thread's or all threads' call stacks (filtered by module or function, threads with the
  same stack grouped)
- modules, registers, locals, and C++ expressions (as `dx` evaluates them)
- memory (read and search), disassembly, source, breakpoints, handles, `!analyze -v`, and any debugger command
- go, step, run to a location, break, wait for the target to stop, and set or remove breakpoints

Every tool call is logged in the Output pane. The token lets a client do anything the debugger can, and DebugX may run
elevated, so keep the token private (**New Token** replaces it).

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
   docking framework, the hex control and the UI helper library. (The `imgui_bundle` and `hello_imgui` submodules are
   not needed to build DebugX.)

2. Install the vcpkg packages, as static libraries, for each platform you build:

   ```
   vcpkg install detours wil scintilla lexilla nlohmann-json --triplet x64-windows-static
   vcpkg install detours wil scintilla lexilla nlohmann-json --triplet x86-windows-static
   ```

3. Open `DebugX.sln` in Visual Studio and build. NuGet restores the other packages (the DbgEng headers and libraries,
   WIL). To build from the command line instead:

   ```
   msbuild DebugX.sln -t:Restore -p:RestorePackagesConfig=true
   msbuild DebugX.sln -p:Configuration=Release -p:Platform=x64
   ```

   The program is `x64\Release\DebugX.exe` (or `Release\DebugX.exe` for x86). Debug and Release build for x64 and x86.

Ignore `CMakeLists.txt`: it is for an unrelated ImGui experiment, not for DebugX.

### Tests

`Tests\DebugEngineTests` (x64) tests the engine wrapper against small test programs that are built with the solution.
Run `x64\Debug\DebugEngineTests.exe` after a Debug build. It runs every suite with each `dbgeng.dll` it can find, and
its exit code is the number of failed checks. Some suites say SKIP when what they need is not there. The kernel suite,
for example, needs an elevated prompt, `bcdedit /debug on` and the kernel's symbols.
