#include "pch.h"
#include "DefaultLayouts.h"
#include "PaneRegistry.h"

using namespace WTLDock;

namespace {
	struct SizeOverride {
		PCWSTR Id;
		SIZE Size;		// at 96 DPI: a width for the left and right sides, a height for the top and bottom
	};

	// User mode:
	//  +-----------------------------+------------------+
	//  |                             | Disassembly      |
	//  |  Command (and the source    |                  |
	//  |  files, as they open)       +------------------+
	//  |                             | Registers        |
	//  |                             | Threads, Modules |
	//  +--------------+--------------+------------------+
	//  | Locals       | Call Stack                      |
	//  | Watch, Memory| Breakpoints, Output             |
	//  +--------------+---------------------------------+
	void BuildUser(DockLayout& layout) {
		auto pane = [&](PCWSTR id) { return layout.FindPane(id); };
		auto beside = [&](PCWSTR id, PCWSTR of, DockPosition pos) { layout.DockTo(pane(id), pane(of)->Group(), pos); };
		auto tabs = [&](PCWSTR with, std::initializer_list<PCWSTR> ids) {
			for (auto id : ids)
				layout.DockTo(pane(id), pane(with)->Group(), DockPosition::Tab);
		};

		layout.Show(pane(L"command"));
		layout.DockToEdge(pane(L"disassembly"), DockSide::Right);
		beside(L"registers", L"disassembly", DockPosition::Bottom);
		tabs(L"registers", { L"threads", L"modules" });
		// (last, so that it spans the width of the window)
		layout.DockToEdge(pane(L"locals"), DockSide::Bottom);
		tabs(L"locals", { L"watch", L"memory" });
		beside(L"callstack", L"locals", DockPosition::Right);
		tabs(L"callstack", { L"breakpoints", L"output" });

		for (auto id : { L"registers", L"locals", L"callstack", L"command" })
			layout.Activate(pane(id));
	}

	// Kernel mode:
	//  +-----------------------------+----------------------------+
	//  |                             | Kernel Processes           |
	//  |  Command                    | Kernel Threads, Drivers,   |
	//  |                             | Object Namespace, IRPs     |
	//  |                             +----------------------------+
	//  |                             | Registers                  |
	//  |                             | Modules, Disassembly       |
	//  +--------------+--------------+----------------------------+
	//  | Call Stack   | Output                                    |
	//  | Locals, Watch| Breakpoints, Memory                       |
	//  +--------------+-------------------------------------------+
	void BuildKernel(DockLayout& layout) {
		auto pane = [&](PCWSTR id) { return layout.FindPane(id); };
		auto beside = [&](PCWSTR id, PCWSTR of, DockPosition pos) { layout.DockTo(pane(id), pane(of)->Group(), pos); };
		auto tabs = [&](PCWSTR with, std::initializer_list<PCWSTR> ids) {
			for (auto id : ids)
				layout.DockTo(pane(id), pane(with)->Group(), DockPosition::Tab);
		};

		layout.Show(pane(L"command"));
		layout.DockToEdge(pane(L"processes"), DockSide::Right);
		tabs(L"processes", { L"kthreads", L"drivers", L"objects", L"irps" });
		beside(L"registers", L"processes", DockPosition::Bottom);
		tabs(L"registers", { L"modules", L"disassembly" });
		layout.DockToEdge(pane(L"callstack"), DockSide::Bottom);
		tabs(L"callstack", { L"locals", L"watch" });
		beside(L"output", L"callstack", DockPosition::Right);
		tabs(L"output", { L"breakpoints", L"memory" });

		for (auto id : { L"processes", L"registers", L"callstack", L"output", L"command" })
			layout.Activate(pane(id));
	}

	// the sizes the first pane at an edge gives its column or row (the others share it)
	const SizeOverride s_UserSizes[] = {
		{ L"disassembly", { 560, 300 } },
		{ L"locals", { 700, 280 } },
	};
	const SizeOverride s_KernelSizes[] = {
		{ L"processes", { 640, 300 } },
		{ L"callstack", { 700, 280 } },
	};
}

std::string BuildDefaultLayout(LayoutMode mode, int dpi) {
	DockLayout layout;
	layout.SetDpi(dpi);
	layout.SetAppVersion(LayoutVersion);

	std::span<const SizeOverride> sizes = mode == LayoutMode::Kernel ? std::span(s_KernelSizes) : std::span(s_UserSizes);
	for (auto& info : PaneRegistry::Get().Panes()) {
		PaneDesc desc;
		desc.Id = info.Id;
		desc.Title = info.Title;
		desc.Kind = info.Kind;
		desc.DefaultSide = info.Side;
		desc.PreferredSize = info.Size;
		for (auto& size : sizes) {
			if (desc.Id == size.Id)
				desc.PreferredSize = size.Size;
		}
		layout.AddPane(desc);
	}

	if (mode == LayoutMode::Kernel)
		BuildKernel(layout);
	else
		BuildUser(layout);
	ATLASSERT(layout.Validate());
	return layout.Save();
}

PCWSTR LayoutModeName(LayoutMode mode) {
	return mode == LayoutMode::Kernel ? L"Kernel Mode" : L"User Mode";
}
