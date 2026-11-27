#pragma once

#include <memory>
#include <string>
#include <QFont>
#include <QString>
#include "IDebugger.h"

class SessionModel;
namespace GdbEngine { class GdbDebugger; }

// the one debugger and its session (DebugSession)
GdbEngine::GdbDebugger& Debugger();
SessionModel& Session();

inline QString Q(const std::wstring& text) {
	return QString::fromStdWString(text);
}
inline std::wstring W(const QString& text) {
	return text.toStdWString();
}
QString HexAddress(ULONG64 address);

// the font of code, addresses and values (monospace) and its setting
QFont CodeFont();
void SetCodeFont(const QFont& font);

//
// A window that starts coroutines keeps one: a coroutine takes 'auto alive = m_Lifetime.Watch();' before its first
// co_await and returns if '!*alive' after each one, before it touches the window (it may be gone by then).
//
class Lifetime {
public:
	Lifetime() : m_Alive(std::make_shared<bool>(true)) {}
	Lifetime(const Lifetime&) = delete;
	~Lifetime() {
		*m_Alive = false;
	}
	std::shared_ptr<bool> Watch() const {
		return m_Alive;
	}

private:
	std::shared_ptr<bool> m_Alive;
};
