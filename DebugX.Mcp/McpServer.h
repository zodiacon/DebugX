#pragma once

//
// An MCP (Model Context Protocol) server that lets an AI client -- Claude Code, Claude Desktop, any MCP client -- investigate
// what DebugX is debugging: threads, call stacks, memory, locals, modules, the engine's own commands, and (if the client's user
// lets it) run, step and set breakpoints. The tools are IDebugger's (McpTools.cpp), so they work with any engine; run_command
// takes the engine's own syntax.
//
// The transport is MCP's Streamable HTTP, on 127.0.0.1 only, at /mcp: each JSON-RPC message is POSTed and answered with JSON
// (no server-sent events; GET is refused, as the transport allows). Every request must carry "Authorization: Bearer <token>":
// any local process can connect to a port on 127.0.0.1, and DebugX may run elevated -- run_command can do whatever the
// debugger can. Origin and Host are checked as well (a web page cannot get through by DNS rebinding).
//
// Threads: connections are served on threads of the server's own; the tools run on the thread the server was started on
// (the UI thread, which is also the debugger's), one at a time, through a window dispatcher of the server's. That thread must
// pump messages.
//

#include <memory>
#include <string>
#include "IDebugger.h"

namespace DebugX::Mcp {
	// What the server needs of the program it is in. Used on the thread the server was started on, only.
	class IMcpHost {
	public:
		virtual ~IMcpHost() = default;

		virtual DebugEngine::IDebugger& Debugger() = 0;
		// the execution status, as the StatusChanged notifications have it
		virtual DebugEngine::ExecutionStatus Status() = 0;
		// what is being debugged, in a few words (a program's file name and pid, a dump's path); empty: not known
		virtual std::wstring TargetName() {
			return {};
		}
		// A client has called a tool, which has completed (or failed, or timed out): for a log the user can see. 'arguments'
		// is the call's arguments as JSON.
		virtual void ToolCalled(const std::wstring& tool, const std::wstring& arguments, bool failed) {}
	};

	class McpServer final {
	public:
		explicit McpServer(IMcpHost& host);
		// (stops it)
		~McpServer();
		McpServer(const McpServer&) = delete;
		McpServer& operator=(const McpServer&) = delete;

		// Listens on 127.0.0.1:port (0: a free port, which Port() then says). 'token' is what the clients must send as their
		// bearer token. Fails with the socket error (WSAEADDRINUSE: the port is taken).
		HRESULT Start(unsigned short port, std::string token);
		// closes the connections and waits for their threads; a tool that is still running in the debugger finishes on its own
		void Stop();
		bool IsRunning() const;
		unsigned short Port() const;
		// http://127.0.0.1:port/mcp
		std::wstring Url() const;

		// a random token (32 bytes, as hex) for Start
		static std::string NewToken();
		// The names of the tools, in the order tools/list gives them
		static std::vector<std::string> ToolNames();

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
