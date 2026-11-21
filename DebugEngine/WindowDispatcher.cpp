#include "pch.h"
#include "Dispatcher.h"

namespace {
	// lParam: the std::function<void()> to run, which the message's receiver deletes
	constexpr UINT WM_DISPATCH = WM_APP + 0x502;
	constexpr PCWSTR WindowClassName = L"DebugEngine.Dispatcher";

	class WindowDispatcher final : public DebugEngine::IDispatcher {
	public:
		~WindowDispatcher() override {
			if (!m_hWnd)
				return;
			// what was posted and not run yet is dropped (the window, and so its messages, belong to this thread)
			MSG msg;
			while (::PeekMessageW(&msg, m_hWnd, WM_DISPATCH, WM_DISPATCH, PM_REMOVE))
				delete reinterpret_cast<std::function<void()>*>(msg.lParam);
			::DestroyWindow(m_hWnd);
		}

		HRESULT Create() {
			auto hInstance = ::GetModuleHandle(nullptr);
			WNDCLASSEXW wc{ sizeof(wc) };
			wc.lpfnWndProc = WindowProc;
			wc.hInstance = hInstance;
			wc.lpszClassName = WindowClassName;
			if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
				return HRESULT_FROM_WIN32(::GetLastError());

			m_hWnd = ::CreateWindowExW(0, WindowClassName, nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr);
			return m_hWnd ? S_OK : HRESULT_FROM_WIN32(::GetLastError());
		}

		bool Post(std::function<void()> func) override {
			auto p = new std::function<void()>(std::move(func));
			if (::PostMessageW(m_hWnd, WM_DISPATCH, 0, reinterpret_cast<LPARAM>(p)))
				return true;
			delete p;
			return false;
		}

	private:
		static LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
			if (msg == WM_DISPATCH) {
				std::unique_ptr<std::function<void()>> f(reinterpret_cast<std::function<void()>*>(lParam));
				(*f)();
				return 0;
			}
			return ::DefWindowProcW(hWnd, msg, wParam, lParam);
		}

		HWND m_hWnd{};
	};
}

HRESULT DebugEngine::CreateWindowDispatcher(std::unique_ptr<IDispatcher>& dispatcher) {
	auto d = std::make_unique<WindowDispatcher>();
	auto hr = d->Create();
	if (SUCCEEDED(hr))
		dispatcher = std::move(d);
	return hr;
}
