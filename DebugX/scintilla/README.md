`ScintillaCall.cxx` is vendored, not written for DebugX: it is the typed C++ call layer that the
Scintilla project ships for C++ consumers to compile into their own app (the `scintilla` vcpkg port
builds the editor itself into `Scintilla.lib`, but deliberately leaves this file as source for each
consumer to add, the same way SciTE does). Taken unmodified from vcpkg's `scintilla` port, version
5.5.8, `call/ScintillaCall.cxx` (matches the `ScintillaCall.h` installed alongside `Scintilla.lib`).

Copyright 1998-2021 by Neil Hodgson <neilh@scintilla.org>. Permission to use, copy, modify and
distribute is granted provided this notice and the one in the file itself are kept; see
https://www.scintilla.org/License.txt for the full text.

To pick up a newer Scintilla, replace this file with `call/ScintillaCall.cxx` from the new version's
source (e.g. `vcpkg`'s `buildtrees/scintilla/src/.../call/ScintillaCall.cxx` after `vcpkg install
scintilla`) and make sure it still matches the `scintilla/ScintillaCall.h` that ships with
`Scintilla.lib` for that version.
