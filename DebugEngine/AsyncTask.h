#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

namespace DebugEngine {
	//
	// Minimal coroutine task type used by DebuggerX's async APIs (e.g. RunAsyncCo).
	// A Task is "hot": the coroutine body runs eagerly until its first real
	// suspension point (an EngineAwaiter queuing work to the debug thread).
	// It can be co_await'ed from another coroutine to get the result.
	//
	// Resumption of a suspended Task is always driven by a single thread's
	// message loop pumping WM_ASYNC_ENGINE_RESUME (see PumpAsyncResume), never
	// from multiple threads concurrently, so the continuation handoff below
	// does not need to be synchronized beyond that.
	//
	// A Task that is destroyed before it completes leaks its coroutine frame
	// rather than risking a resume into freed memory: the underlying engine
	// call has already been queued to the debug thread and cannot be safely
	// cancelled once in flight.
	//
	template <typename T>
	class Task {
	public:
		struct promise_type {
			std::optional<T> _value;
			std::exception_ptr _exception;
			std::coroutine_handle<> _continuation;

			Task get_return_object() noexcept {
				return Task(std::coroutine_handle<promise_type>::from_promise(*this));
			}
			std::suspend_never initial_suspend() noexcept { return {}; }
			auto final_suspend() noexcept {
				struct Awaiter {
					bool await_ready() noexcept { return false; }
					std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
						auto continuation = h.promise()._continuation;
						return continuation ? continuation : std::noop_coroutine();
					}
					void await_resume() noexcept {}
				};
				return Awaiter{};
			}
			void return_value(T value) { _value = std::move(value); }
			void unhandled_exception() { _exception = std::current_exception(); }
		};

		using Handle = std::coroutine_handle<promise_type>;

		Task() noexcept = default;
		explicit Task(Handle handle) noexcept : m_Handle(handle) {}
		Task(Task&& other) noexcept : m_Handle(std::exchange(other.m_Handle, {})) {}
		Task& operator=(Task&& other) noexcept {
			if (this != &other) {
				if (m_Handle && m_Handle.done())
					m_Handle.destroy();
				m_Handle = std::exchange(other.m_Handle, {});
			}
			return *this;
		}
		Task(const Task&) = delete;
		Task& operator=(const Task&) = delete;

		~Task() {
			if (m_Handle && m_Handle.done())
				m_Handle.destroy();
		}

		bool await_ready() const noexcept { return m_Handle.done(); }
		void await_suspend(std::coroutine_handle<> awaiting) noexcept {
			m_Handle.promise()._continuation = awaiting;
		}
		T await_resume() {
			if (m_Handle.promise()._exception)
				std::rethrow_exception(m_Handle.promise()._exception);
			return std::move(*m_Handle.promise()._value);
		}

	private:
		Handle m_Handle;
	};

	template <>
	class Task<void> {
	public:
		struct promise_type {
			std::exception_ptr _exception;
			std::coroutine_handle<> _continuation;

			Task get_return_object() noexcept {
				return Task(std::coroutine_handle<promise_type>::from_promise(*this));
			}
			std::suspend_never initial_suspend() noexcept { return {}; }
			auto final_suspend() noexcept {
				struct Awaiter {
					bool await_ready() noexcept { return false; }
					std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
						auto continuation = h.promise()._continuation;
						return continuation ? continuation : std::noop_coroutine();
					}
					void await_resume() noexcept {}
				};
				return Awaiter{};
			}
			void return_void() noexcept {}
			void unhandled_exception() { _exception = std::current_exception(); }
		};

		using Handle = std::coroutine_handle<promise_type>;

		Task() noexcept = default;
		explicit Task(Handle handle) noexcept : m_Handle(handle) {}
		Task(Task&& other) noexcept : m_Handle(std::exchange(other.m_Handle, {})) {}
		Task& operator=(Task&& other) noexcept {
			if (this != &other) {
				if (m_Handle && m_Handle.done())
					m_Handle.destroy();
				m_Handle = std::exchange(other.m_Handle, {});
			}
			return *this;
		}
		Task(const Task&) = delete;
		Task& operator=(const Task&) = delete;

		~Task() {
			if (m_Handle && m_Handle.done())
				m_Handle.destroy();
		}

		bool await_ready() const noexcept { return m_Handle.done(); }
		void await_suspend(std::coroutine_handle<> awaiting) noexcept {
			m_Handle.promise()._continuation = awaiting;
		}
		void await_resume() {
			if (m_Handle.promise()._exception)
				std::rethrow_exception(m_Handle.promise()._exception);
		}

	private:
		Handle m_Handle;
	};

	// Pumps one message that resumes a Task suspended on an engine operation.
	// Call this from a Win32 message loop (before normal dispatch) on any
	// thread that awaits DebuggerX's coroutine APIs (e.g. RunAsyncCo).
	// Returns true if the message was consumed and should not be dispatched further.
	bool PumpAsyncResume(const MSG& msg);
}
