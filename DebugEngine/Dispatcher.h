#pragma once

#include <functional>
#include <memory>

namespace DebugEngine {
	//
	// Runs functions on one thread -- the one a debugger belongs to, the UI thread -- from its event loop, in the order they
	// were posted. A debugger posts the completions of its calls (which resume the coroutines that await them) and its
	// notifications through one (IDebugger::Create). A GUI toolkit other than Win32's supplies its own (Qt: a queued
	// QMetaObject::invokeMethod); the debugger destroys it on its thread.
	//
	class IDispatcher {
	public:
		virtual ~IDispatcher() = default;
		// Queues the function to run on the dispatcher's thread (any thread may post). False if it cannot be; the function
		// is then dropped. Functions still queued when the dispatcher is destroyed are dropped, not run.
		virtual bool Post(std::function<void()> func) = 0;
	};

#ifdef _WIN32
	// A dispatcher on the calling thread: a message-only window whose messages run the functions, so it works under any
	// message loop the thread runs, including a modal dialog's.
	HRESULT CreateWindowDispatcher(std::unique_ptr<IDispatcher>& dispatcher);
#endif
}
