#pragma once

#include <string>
#include <vector>

// What was debugged lately (File > Recent Targets): kept in AppSettings::RecentTargets, newest first, a line for each.
struct RecentTarget {
	enum class Kind {
		Run,			// Path: the executable (with Arguments, WorkingDir)
		Attach,			// Path: the process's image name (notepad.exe): a process ID does not last
		Dump,			// Path: the dump file
		Kernel,			// Path: the connection string
		LocalKernel
	};
	Kind Type{ Kind::Run };
	std::wstring Path, Arguments, WorkingDir;

	bool operator==(const RecentTarget&) const = default;
	// as the menu has it (without the number)
	std::wstring MenuText() const;
};

namespace RecentTargets {
	constexpr size_t MaxCount = 10;

	std::vector<RecentTarget> Load();
	// to the top of the list (once: an equal one further down is dropped), and saved
	void Add(RecentTarget target);
	void Clear();
}
