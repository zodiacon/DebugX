#include "GdbProcess.h"
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

extern char** environ;

namespace GdbEngine {
	namespace {
		void CloseFd(int& fd) {
			if (fd >= 0) {
				::close(fd);
				fd = -1;
			}
		}
	}

	GdbProcess::~GdbProcess() {
		Stop();
		if (m_Log)
			std::fclose(m_Log);
	}

	void GdbProcess::Log(char direction, std::string_view line) {
		if (!m_Log)
			return;
		using namespace std::chrono;
		auto ms = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
		std::lock_guard lock(m_LogLock);
		std::fprintf(m_Log, "%lld.%03lld %c %.*s\n", static_cast<long long>(ms / 1000), static_cast<long long>(ms % 1000), direction,
			static_cast<int>(line.size()), line.data());
		std::fflush(m_Log);
	}

	bool GdbProcess::Start(Callbacks callbacks, std::string gdb) {
		if (m_Running)
			return false;
		m_Callbacks = std::move(callbacks);
		if (auto log = ::getenv("DEBUGX_MI_LOG"); log && *log && !m_Log)
			m_Log = std::fopen(log, "a");
		if (gdb.empty()) {
			auto env = ::getenv("DEBUGX_GDB");
			gdb = env && *env ? env : "gdb";
		}

		// the programs' terminal: raw, so that their output comes as they wrote it (no \r added, no echo)
		m_PtyMaster = ::posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
		if (m_PtyMaster < 0 || ::grantpt(m_PtyMaster) != 0 || ::unlockpt(m_PtyMaster) != 0)
			return false;
		m_TtyName = ::ptsname(m_PtyMaster);
		// (kept open here too, so that reading the master does not fail with EIO between programs)
		m_PtySlave = ::open(m_TtyName.c_str(), O_RDWR | O_NOCTTY | O_CLOEXEC);
		if (m_PtySlave >= 0) {
			termios tio{};
			if (::tcgetattr(m_PtySlave, &tio) == 0) {
				::cfmakeraw(&tio);
				::tcsetattr(m_PtySlave, TCSANOW, &tio);
			}
		}

		int in[2], out[2], wake[2];
		if (::pipe2(in, O_CLOEXEC) != 0)
			return false;
		if (::pipe2(out, O_CLOEXEC) != 0) {
			::close(in[0]); ::close(in[1]);
			return false;
		}
		if (::pipe2(wake, O_CLOEXEC) != 0) {
			::close(in[0]); ::close(in[1]); ::close(out[0]); ::close(out[1]);
			return false;
		}

		posix_spawn_file_actions_t actions;
		posix_spawn_file_actions_init(&actions);
		posix_spawn_file_actions_adddup2(&actions, in[0], 0);
		posix_spawn_file_actions_adddup2(&actions, out[1], 1);
		posix_spawn_file_actions_adddup2(&actions, out[1], 2);
		posix_spawnattr_t attr;
		posix_spawnattr_init(&attr);
		// its own process group: a Ctrl+C in the terminal DebugX was started from is not GDB's
		posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
		posix_spawnattr_setpgroup(&attr, 0);

		std::string interp = "--interpreter=mi3";
		char* argv[] = { gdb.data(), interp.data(), const_cast<char*>("--nx"), const_cast<char*>("-q"), nullptr };
		int rc = ::posix_spawnp(&m_Pid, gdb.c_str(), &actions, &attr, argv, environ);
		posix_spawn_file_actions_destroy(&actions);
		posix_spawnattr_destroy(&attr);
		::close(in[0]);
		::close(out[1]);
		if (rc != 0) {
			::close(in[1]); ::close(out[0]); ::close(wake[0]); ::close(wake[1]);
			m_Pid = -1;
			return false;
		}
		m_In = in[1];
		m_Out = out[0];
		m_WakeRead = wake[0];
		m_WakeWrite = wake[1];
		m_Running = true;
		m_Stopping = false;
		m_Reader = std::thread([this] { ReaderThread(); });
		return true;
	}

	bool GdbProcess::Write(std::string_view line) {
		std::lock_guard lock(m_WriteLock);
		if (!m_Running || m_In < 0)
			return false;
		Log('>', line);
		std::string data(line);
		data += '\n';
		const char* p = data.data();
		size_t left = data.size();
		while (left > 0) {
			auto n = ::write(m_In, p, left);
			if (n < 0) {
				if (errno == EINTR)
					continue;
				return false;
			}
			p += n;
			left -= n;
		}
		return true;
	}

	void GdbProcess::ReaderThread() {
		std::string line;
		char buffer[8192];
		bool gdbOpen = true;
		while (gdbOpen) {
			pollfd fds[3] = { { m_Out, POLLIN, 0 }, { m_PtyMaster, POLLIN, 0 }, { m_WakeRead, POLLIN, 0 } };
			if (::poll(fds, 3, -1) < 0) {
				if (errno == EINTR)
					continue;
				break;
			}
			if (fds[2].revents)
				break;
			// the program's output first: what it wrote before it stopped comes before the stop
			if (fds[1].revents & POLLIN) {
				auto n = ::read(m_PtyMaster, buffer, sizeof(buffer));
				if (n > 0 && m_Callbacks.InferiorOutput)
					m_Callbacks.InferiorOutput(std::string(buffer, n));
			}
			if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
				auto n = ::read(m_Out, buffer, sizeof(buffer));
				if (n <= 0) {
					if (n < 0 && errno == EINTR)
						continue;
					gdbOpen = false;
					break;
				}
				for (ssize_t i = 0; i < n; i++) {
					if (buffer[i] == '\n') {
						Log('<', line);
						if (!line.empty() && m_Callbacks.Record)
							m_Callbacks.Record(ParseMiLine(line));
						line.clear();
					}
					else
						line += buffer[i];
				}
			}
		}
		m_Running = false;
		if (!m_Stopping && m_Callbacks.Exited)
			m_Callbacks.Exited();
	}

	void GdbProcess::Stop() {
		if (m_Pid < 0)
			return;
		m_Stopping = true;
		if (m_Running) {
			Write("-gdb-exit");
			// GDB kills what it runs as it exits; give it a moment
			auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
			int status;
			bool exited = false;
			while (std::chrono::steady_clock::now() < until) {
				if (::waitpid(m_Pid, &status, WNOHANG) == m_Pid) {
					exited = true;
					break;
				}
				::usleep(10000);
			}
			if (!exited) {
				::kill(m_Pid, SIGKILL);
				::waitpid(m_Pid, &status, 0);
			}
		}
		else {
			int status;
			::waitpid(m_Pid, &status, 0);
		}
		if (m_WakeWrite >= 0) {
			char c = 1;
			(void)::write(m_WakeWrite, &c, 1);
		}
		if (m_Reader.joinable())
			m_Reader.join();
		{
			std::lock_guard lock(m_WriteLock);
			m_Running = false;
			CloseFd(m_In);
		}
		CloseFd(m_Out);
		CloseFd(m_WakeRead);
		CloseFd(m_WakeWrite);
		CloseFd(m_PtySlave);
		CloseFd(m_PtyMaster);
		m_Pid = -1;
	}
}
