#pragma once

#include <memory>

//
// A coroutine that continues after an engine call can outlive the window it belongs to: the call is still queued
// when the window is destroyed and resumes it later. A window that starts coroutines keeps one of these, and each
// coroutine takes a copy of the flag before its first co_await and checks it after every one, before touching the
// window (or anything else the window owns):
//
//   auto alive = m_Lifetime.Watch();
//   co_await DebugManager::DebuggerX().Something();
//   if (!*alive)
//       co_return;
//
class Lifetime final {
public:
	Lifetime() = default;
	Lifetime(const Lifetime&) = delete;
	Lifetime& operator=(const Lifetime&) = delete;

	~Lifetime() {
		*m_Alive = false;
	}

	std::shared_ptr<const bool> Watch() const {
		return m_Alive;
	}

private:
	std::shared_ptr<bool> m_Alive = std::make_shared<bool>(true);
};
