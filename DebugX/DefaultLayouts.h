#pragma once

#include <string>

//
// The arrangements of the panes DebugX comes with. There is one for each kind of target: the frame keeps the layout
// of each mode (the user's own changes to it included) and switches to the other one when a target of the other kind
// is attached to; Window > Reset Window Layout goes back to the current mode's default.
//
enum class LayoutMode {
	User,
	Kernel,
};

// saved with every layout; layout.json files of an older version are not restored (they predate the defaults)
constexpr int LayoutVersion = 1;

// The default layout of a mode, as DockLayout text, at the given DPI. Built on a DockLayout of its own (it has no
// windows), so the frame's panes are made only when it is loaded.
std::string BuildDefaultLayout(LayoutMode mode, int dpi);

PCWSTR LayoutModeName(LayoutMode mode);
