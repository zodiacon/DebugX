#include <QApplication>
#include <QMessageBox>
#include <QTimer>
#include "DebugSession.h"
#include "GdbDebugger.h"
#include "MainWindow.h"

namespace {
	// For looking at the GUI without a person (QT_QPA_PLATFORM=offscreen): DEBUGX_COMMANDS (';'-separated GDB commands, run
	// once the target has started) then DEBUGX_SCREENSHOT (a .png of the window, after DEBUGX_SCREENSHOT_DELAY ms) and exit.
	DebugEngine::Task<void> SmokeTest(MainWindow* window, QStringList commands, QString screenshot, int delay) {
		auto wait = [](int ms) {
			struct Awaiter {
				int Ms;
				bool await_ready() const noexcept { return false; }
				void await_suspend(std::coroutine_handle<> h) { QTimer::singleShot(Ms, [h] { h.resume(); }); }
				void await_resume() noexcept {}
			};
			return Awaiter{ ms };
		};
		for (int i = 0; i < 100 && !Session().HasTarget(); i++)
			co_await wait(100);
		co_await wait(500);
		for (auto& command : commands)
			co_await Debugger().ExecuteCommand(W(command.trimmed()).c_str());
		co_await wait(delay);
		if (!screenshot.isEmpty())
			window->grab().save(screenshot);
		QApplication::quit();
	}
}

int main(int argc, char* argv[]) {
	QApplication app(argc, argv);
	QApplication::setOrganizationName("DebugX");
	QApplication::setApplicationName("DebugX");
	QApplication::setApplicationVersion("0.1");

	DebugSession session;
	if (!session.Init()) {
		QMessageBox::critical(nullptr, "DebugX", "GDB could not be started. Install it (sudo apt install gdb), or set DEBUGX_GDB to the one to use.");
		return 1;
	}

	int result;
	{
		MainWindow window;
		window.show();
		auto arguments = app.arguments().mid(1);
		// once the window is shown
		QTimer::singleShot(0, &window, [&window, arguments] { window.StartFromCommandLine(arguments); });
		auto screenshot = qEnvironmentVariable("DEBUGX_SCREENSHOT");
		if (!screenshot.isEmpty())
			SmokeTest(&window, qEnvironmentVariable("DEBUGX_COMMANDS").split(';', Qt::SkipEmptyParts), screenshot,
				qEnvironmentVariableIntValue("DEBUGX_SCREENSHOT_DELAY") ? qEnvironmentVariableIntValue("DEBUGX_SCREENSHOT_DELAY") : 1500);
		result = app.exec();
	}
	return result;
}
