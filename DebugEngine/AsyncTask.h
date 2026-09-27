#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <type_traits>
#include <utility>

namespace DebugEngine {
	//
	// Minimal coroutine task type used by DebuggerX's async APIs.
	//
	// A Task is "hot": the coroutine body runs eagerly until its first real
	// suspension point (an engine call queued to the debug thread). It can be
	// co_await'ed from another coroutine to get the result, or simply dropped
	// when the caller is not interested in it: a Task that is destroyed before
	// it completes lets the coroutine run to the end and then frees its frame,
	// so the outcome of a "fire and forget" call is discarded, not leaked.
	//
	// Coroutines are only ever resumed on the thread that created the debugger
	// (from its message loop), never concurrently, so the handoff below does
	// not need synchronization.
	//
	// Anything a coroutine touches after a co_await (a window, say) may be gone
	// by the time it resumes; see Lifetime.h in the GUI project.
	//
	template<typename T>
	class Task;

	namespace detail {
		struct FinalAwaiter {
			bool await_ready() noexcept {
				return false;
			}

			template<typename P>
			std::coroutine_handle<> await_suspend(std::coroutine_handle<P> handle) noexcept {
				auto& promise = handle.promise();
				if (promise.Detached) {
					// nobody is waiting for the result
					handle.destroy();
					return std::noop_coroutine();
				}
				return promise.Continuation ? promise.Continuation : std::noop_coroutine();
			}

			void await_resume() noexcept {}
		};

		struct PromiseBase {
			std::exception_ptr Exception;
			std::coroutine_handle<> Continuation;
			bool Detached{ false };

			std::suspend_never initial_suspend() noexcept {
				return {};
			}

			FinalAwaiter final_suspend() noexcept {
				return {};
			}

			void unhandled_exception() {
				Exception = std::current_exception();
			}
		};

		template<typename T>
		struct Promise : PromiseBase {
			std::optional<T> Value;

			Task<T> get_return_object() noexcept;
			void return_value(T value) {
				Value = std::move(value);
			}
		};

		template<>
		struct Promise<void> : PromiseBase {
			Task<void> get_return_object() noexcept;
			void return_void() noexcept {}
		};
	}

	template<typename T>
	class Task {
	public:
		using promise_type = detail::Promise<T>;
		using Handle = std::coroutine_handle<promise_type>;

		Task() noexcept = default;
		explicit Task(Handle handle) noexcept : m_Handle(handle) {}
		Task(Task&& other) noexcept : m_Handle(std::exchange(other.m_Handle, {})) {}
		Task& operator=(Task&& other) noexcept {
			if (this != &other) {
				Release();
				m_Handle = std::exchange(other.m_Handle, {});
			}
			return *this;
		}
		Task(const Task&) = delete;
		Task& operator=(const Task&) = delete;

		~Task() {
			Release();
		}

		bool await_ready() const noexcept {
			return m_Handle.done();
		}

		void await_suspend(std::coroutine_handle<> awaiting) noexcept {
			m_Handle.promise().Continuation = awaiting;
		}

		T await_resume() {
			auto& promise = m_Handle.promise();
			if (promise.Exception)
				std::rethrow_exception(promise.Exception);
			if constexpr (!std::is_void_v<T>)
				return std::move(*promise.Value);
		}

	private:
		void Release() noexcept {
			if (m_Handle) {
				if (m_Handle.done())
					m_Handle.destroy();
				else
					m_Handle.promise().Detached = true;
				m_Handle = {};
			}
		}

		Handle m_Handle;
	};

	namespace detail {
		template<typename T>
		Task<T> Promise<T>::get_return_object() noexcept {
			return Task<T>(std::coroutine_handle<Promise<T>>::from_promise(*this));
		}

		inline Task<void> Promise<void>::get_return_object() noexcept {
			return Task<void>(std::coroutine_handle<Promise<void>>::from_promise(*this));
		}
	}
}
