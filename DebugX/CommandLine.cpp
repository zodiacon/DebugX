#include "pch.h"
#include "CommandLine.h"
#include <shellapi.h>
#include <vector>

namespace {
	// (an argument back into a command line, for the program's own: CommandLineToArgvW's rules)
	std::wstring Quoted(const std::wstring& arg) {
		if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos)
			return arg;
		std::wstring text(L"\"");
		size_t backslashes = 0;
		for (auto c : arg) {
			if (c == L'\\') {
				backslashes++;
				continue;
			}
			// backslashes before a quote are doubled, and the quote escaped
			text.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
			backslashes = 0;
			text += c;
		}
		text.append(backslashes * 2, L'\\');
		return text + L'"';
	}

	bool IsDump(const std::wstring& path) {
		auto dot = path.find_last_of(L'.');
		if (dot == std::wstring::npos)
			return false;
		auto ext = path.substr(dot);
		return _wcsicmp(ext.c_str(), L".dmp") == 0 || _wcsicmp(ext.c_str(), L".mdmp") == 0 || _wcsicmp(ext.c_str(), L".hdmp") == 0;
	}
}

PCWSTR CommandLine::Usage() {
	return L"DebugX.exe program.exe [arguments]\trun the program under the debugger\n"
		L"DebugX.exe file.dmp\t\topen the dump file (or -z file.dmp)\n"
		L"-p <pid>\t\t\tattach to the process\n"
		L"-pn <name>\t\tattach to the one process of that name\n"
		L"-e <event>\t\t(with -p) signal the event once attached (AeDebug)\n"
		L"-g\t\t\tgo on past the initial break\n"
		L"-k <connection>\t\ta kernel over a connection (net:port=50000,key=a.b.c.d, ...)\n"
		L"-kl\t\t\tthe local kernel\n\n"
		L"As the postmortem debugger: DebugX.exe -p %ld -e %ld -g";
}

CommandLine CommandLine::Parse(PCWSTR commandLine) {
	CommandLine result;
	if (!commandLine || !*commandLine)
		return result;

	// (CommandLineToArgvW parses the first word as the program's own name, by other rules; WinMain's line does not have
	// it, so one is put in front)
	int argc = 0;
	auto argv = ::CommandLineToArgvW((std::wstring(L"DebugX.exe ") + commandLine).c_str(), &argc);
	if (!argv)
		return result;
	std::vector<std::wstring> args(argv + 1, argv + argc);
	::LocalFree(argv);

	auto fail = [&](std::wstring error) {
		result = {};
		result.Type = Kind::Help;
		result.Error = std::move(error);
		return result;
		};
	auto set = [&](Kind kind) {
		if (result.Type != Kind::None && result.Type != kind)
			return false;
		result.Type = kind;
		return true;
		};

	for (size_t i = 0; i < args.size(); i++) {
		auto& arg = args[i];
		auto value = [&](PCWSTR what) -> const std::wstring* {
			return i + 1 < args.size() ? &args[++i] : nullptr;
			};
		if (arg.size() > 1 && (arg[0] == L'-' || arg[0] == L'/')) {
			auto option = arg.substr(1);
			if (option == L"?" || _wcsicmp(option.c_str(), L"h") == 0 || _wcsicmp(option.c_str(), L"help") == 0)
				return fail({});
			if (option == L"p") {
				auto v = value(L"pid");
				wchar_t* end = nullptr;
				auto pid = v ? ::wcstoul(v->c_str(), &end, 0) : 0;
				if (!v || pid == 0 || *end)
					return fail(L"-p needs a process ID");
				if (!set(Kind::Attach))
					return fail(L"-p goes with -e and -g only");
				result.Pid = pid;
			}
			else if (option == L"pn") {
				auto v = value(L"name");
				if (!v || !set(Kind::AttachByName))
					return fail(L"-pn needs a process name, and nothing else to start");
				result.Path = *v;
			}
			else if (option == L"e") {
				auto v = value(L"event");
				wchar_t* end = nullptr;
				auto handle = v ? ::wcstoull(v->c_str(), &end, 0) : 0;
				if (!v || handle == 0 || *end)
					return fail(L"-e needs an event handle");
				result.Event = static_cast<ULONG_PTR>(handle);
			}
			else if (option == L"g")
				result.Go = true;
			else if (option == L"z") {
				auto v = value(L"dump");
				if (!v || !set(Kind::Dump))
					return fail(L"-z needs a dump file, and nothing else to start");
				result.Path = *v;
			}
			else if (option == L"k") {
				auto v = value(L"connection");
				if (!v || !set(Kind::Kernel))
					return fail(L"-k needs a connection (net:port=50000,key=a.b.c.d, ...)");
				result.Path = *v;
			}
			else if (option == L"kl") {
				if (!set(Kind::LocalKernel))
					return fail(L"-kl goes with nothing else to start");
			}
			else
				return fail(L"Unknown option: " + arg);
			continue;
		}
		// the first word that is not an option: a dump, or a program (and what follows is the program's)
		if (result.Type != Kind::None)
			return fail(L"Unexpected: " + arg);
		result.Path = arg;
		if (IsDump(arg) && i + 1 == args.size()) {
			result.Type = Kind::Dump;
			break;
		}
		result.Type = Kind::Run;
		for (size_t j = i + 1; j < args.size(); j++) {
			if (!result.Arguments.empty())
				result.Arguments += L' ';
			result.Arguments += Quoted(args[j]);
		}
		break;
	}
	if (result.Event && result.Type != Kind::Attach)
		return fail(L"-e goes with -p");
	return result;
}
