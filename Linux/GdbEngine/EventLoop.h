#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include "Dispatcher.h"

//
// A minimal event loop for a program without a GUI toolkit (the tests, a command-line front end): functions posted from
// any thread run on the thread that calls Run/RunUntil. CreateDispatcher gives an IDispatcher for IDebugger::Create
// that posts to it; when that dispatcher is destroyed (the debugger does, on shutdown), what it posted and has not run yet
// is dropped. (The Qt GUI uses a dispatcher of its own.)
//
namespace GdbEngine {
	class EventLoop {
	public:
		void Post(std::function<void()> func);
		// runs what is posted until 'done' is true (checked after each function) or the time is up; false: timed out
		bool RunUntil(const std::function<bool()>& done, std::chrono::milliseconds timeout = std::chrono::milliseconds(30000));
		// runs what is posted for a while
		void RunFor(std::chrono::milliseconds time);

		std::unique_ptr<DebugEngine::IDispatcher> CreateDispatcher();

	private:
		std::mutex m_Lock;
		std::condition_variable m_Cv;
		std::deque<std::function<void()>> m_Queue;
	};
}
