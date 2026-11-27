#pragma once

#include <QCoreApplication>
#include <QObject>
#include <QMetaObject>
#include "Dispatcher.h"

//
// IDispatcher for Qt: a function posted from any thread runs on the thread that made the dispatcher (the GUI thread),
// from its event loop -- a queued call on a context object of the dispatcher's own. Qt drops the queued calls of a
// deleted object, so what is still queued when the debugger destroys the dispatcher does not run.
//
class QtDispatcher final : public DebugEngine::IDispatcher {
public:
	QtDispatcher() : m_Context(new QObject) {}
	~QtDispatcher() override {
		delete m_Context;
	}

	bool Post(std::function<void()> func) override {
		return QMetaObject::invokeMethod(m_Context, std::move(func), Qt::QueuedConnection);
	}

private:
	QObject* m_Context;
};
