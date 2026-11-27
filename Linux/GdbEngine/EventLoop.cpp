#include "EventLoop.h"
#include <atomic>

namespace GdbEngine {
	namespace {
		class LoopDispatcher : public DebugEngine::IDispatcher {
		public:
			explicit LoopDispatcher(EventLoop& loop) : m_Loop(loop), m_Alive(std::make_shared<std::atomic<bool>>(true)) {}
			~LoopDispatcher() override {
				*m_Alive = false;
			}

			bool Post(std::function<void()> func) override {
				if (!*m_Alive)
					return false;
				m_Loop.Post([alive = m_Alive, func = std::move(func)] {
					if (*alive)
						func();
				});
				return true;
			}

		private:
			EventLoop& m_Loop;
			std::shared_ptr<std::atomic<bool>> m_Alive;
		};
	}

	void EventLoop::Post(std::function<void()> func) {
		{
			std::lock_guard lock(m_Lock);
			m_Queue.push_back(std::move(func));
		}
		m_Cv.notify_one();
	}

	bool EventLoop::RunUntil(const std::function<bool()>& done, std::chrono::milliseconds timeout) {
		auto until = std::chrono::steady_clock::now() + timeout;
		while (!done()) {
			std::function<void()> func;
			{
				std::unique_lock lock(m_Lock);
				if (!m_Cv.wait_until(lock, until, [this] { return !m_Queue.empty(); }))
					return done();
				func = std::move(m_Queue.front());
				m_Queue.pop_front();
			}
			func();
		}
		return true;
	}

	void EventLoop::RunFor(std::chrono::milliseconds time) {
		RunUntil([] { return false; }, time);
	}

	std::unique_ptr<DebugEngine::IDispatcher> EventLoop::CreateDispatcher() {
		return std::make_unique<LoopDispatcher>(*this);
	}
}
