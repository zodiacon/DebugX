#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <sys/types.h>
#include "MiParser.h"

//
// GDB as a child process speaking GDB/MI (gdb --interpreter=mi3), and a pseudo-terminal for the programs it runs, so that
// their output never mixes with GDB's. One thread reads both (poll) and hands each MI record, and each piece of the
// program's output, to the callbacks -- on that thread; the caller moves them to its own.
//
namespace GdbEngine {
	class GdbProcess {
	public:
		struct Callbacks {
			std::function<void(MiRecord&&)> Record;
			std::function<void(std::string&&)> InferiorOutput;
			std::function<void()> Exited;		// GDB has gone (after the last record)
		};

		GdbProcess() = default;
		GdbProcess(const GdbProcess&) = delete;
		GdbProcess& operator=(const GdbProcess&) = delete;
		~GdbProcess();

		// 'gdb': the program (looked up in PATH); empty: $DEBUGX_GDB, else "gdb"
		bool Start(Callbacks callbacks, std::string gdb = {});
		// a line to GDB (the line end is added); any thread
		bool Write(std::string_view line);
		// asks GDB to exit, waits a little, then kills it; joins the reader. Calls no callback after it returns.
		void Stop();

		bool IsRunning() const {
			return m_Running;
		}
		// the terminal the debugged programs get (-inferior-tty-set)
		const std::string& InferiorTty() const {
			return m_TtyName;
		}
		pid_t Pid() const {
			return m_Pid;
		}

	private:
		void ReaderThread();

		Callbacks m_Callbacks;
		pid_t m_Pid{ -1 };
		int m_In{ -1 }, m_Out{ -1 };		// GDB's stdin (we write), stdout+stderr (we read)
		int m_PtyMaster{ -1 }, m_PtySlave{ -1 };
		int m_WakeRead{ -1 }, m_WakeWrite{ -1 };
		std::string m_TtyName;
		std::thread m_Reader;
		std::mutex m_WriteLock;
		std::atomic<bool> m_Running{ false };
		std::atomic<bool> m_Stopping{ false };
	};
}
