# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

DebugX is a Windows-only GUI/CLI debugger front-end built on top of the Windows Debug Engine (DbgEng/`IDebugClient` COM APIs — `DbgEng.h`/`DbgModel.h`). It wraps `dbgeng.dll` to attach to live user-mode processes, launch executables under the debugger, open crash dumps, and attach to a local kernel debugger, then exposes process/thread/module enumeration and command execution to a WTL-based MDI GUI.

## Build

This is a Visual Studio solution (`DebugX.sln`) with four C++ projects, built via MSBuild/Visual Studio (not the top-level `CMakeLists.txt` — that file only builds an unrelated `Project1` ImGui scratch/demo app and references a `Test2` subdirectory that does not exist in this repo; ignore it unless working specifically on the ImGui experiment).

Dependencies are restored via NuGet (`packages.config` per project): WTL, WIL (`Microsoft.Windows.ImplementationLibrary`), and `Microsoft.Debugging.DataModel.CppLib` / `Microsoft.Debugging.Platform.DbgEng` (DbgEng headers/import libs). Open `DebugX.sln` in Visual Studio and build/run normally (x86/x64, Debug/Release configs); there is no CLI build script or test suite in this repo.

## Projects

- **DebugEngine** (static lib) — the core wrapper around DbgEng. `Debugger.h`/`.cpp` defines `DebugEngine::DebuggerX`, the pImpl-based public API (`DebuggerX::Impl`) that owns the `IDebugClient7`/`IDebugControl7`/etc. COM pointers.
- **DebugX.Kernel** (static lib) — kernel-mode analysis helpers used when attached to a live/local kernel target (`DebugX::Analysis` namespace): `KernelGlobals` (symbol/global lookup), `KernelProcessAnalyzer`/`KernelProcessEnumerator` (walks `_EPROCESS` via the debugger's symbol/data-space APIs), `KernelThreadAnalyzer`/`KernelProcessThreadEnumerator`. These are plain analysis classes, not an actual kernel driver, despite the project name.
- **DebugX** — the WTL/ATL MDI GUI app (`CMainFrame`, `*View.cpp` tab views such as `KernelProcessesView`, `UserThreadsView`, `CommandView`). `DebugManager` is a process-wide singleton wrapping a single `DebuggerX` instance and a notification thread; views register/unregister their `HWND` with it to receive debugger notifications.
- **CDebugX** — a standalone console test/reference harness (`wmain`) that drives DbgEng directly (bypassing `DebuggerX`) to attach by PID, open a dump, or attach to the local kernel and run an interactive command loop. Useful as a minimal reference for DbgEng call sequences, but is not wired to the `DebugEngine` library.

Also present: `hello_imgui` and `imgui_bundle` git submodules (pulled in for the unrelated `Project1` ImGui experiment, not part of the debugger itself).

## Architecture: threading model

DbgEng requires all calls to `IDebugClient`/`IDebugControl` etc. for a given client to happen on the thread that created the client. `DebuggerX::Impl` runs a **dedicated debugger thread** (`DebuggerThread()`, created in `Create()`) and marshals every engine call onto it via `QueueUserAPC` + the `RunAsync(...)` template overloads (0/1/2-argument variants) in `Debugger.h`/`.cpp`. Callers on any other thread invoke public `DebuggerX` methods, which block on `m_hAsyncEvent` while pumping window messages (`WaitWithMessages`) so the UI stays responsive during long engine calls.

Engine events (breakpoints, exceptions, module load/unload, process/thread create/exit, output text, execution-status changes) arrive on `IDebugEventCallbacks`/`IDebugOutputCallbacks2` implemented by `Impl` itself, and are forwarded as Windows messages via `PostNotification()`/`PostThreadMessage`/`PostMessage` to whichever thread/HWND was registered in `Create()`. Message `WPARAM`/`LPARAM` carry a heap-allocated data struct (e.g. `BreakpointData`, `CreateProcessData`, `OutputData`); the receiver **must** call `DebuggerX::FreeNotification(wParam)` after handling the message or the allocation leaks.

`DebugManager` (in the `DebugX` GUI project) is the app-wide singleton gluing this together: it owns the one `DebuggerX`, runs the notification thread, and lets multiple view windows (implementing `IDebugXView`) register to receive notifications via `RegisterWindow`/`UnregisterWindow`.

## Conventions to know

- `TargetType` is a flags enum (`DEFINE_ENUM_FLAG_OPERATORS`) — check kernel vs. user targets with bitwise `&` (e.g. `GetTargetType() & TargetType::Kernel`), not equality.
- The `HR(x)` macro (defined locally in each `.cpp` that uses it) early-returns the enclosing function on a failed `HRESULT` — it assumes the enclosing lambda/function returns `HRESULT`.
- Kernel process/thread walking (`DebugX.Kernel`) reads target memory/symbols directly (e.g. `_EPROCESS` linked lists) rather than using `IDebugSystemObjects`, since that API only works for live user-mode/live-kernel targets, not always applicable — check `KernelAnalyzerBase`/`KernelGlobals` before assuming a helper works for dumps.
