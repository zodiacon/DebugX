#pragma once

#include <QWidget>
#include "Common.h"
#include "SessionModel.h"

class QLineEdit;
class CommandOutput;

//
// The Command view (the one document that is always there): GDB's console. A command typed in goes to GDB as it is
// (ExecuteCommand: -interpreter-exec console), its output and everything GDB and the program say come here, into a
// read-only Scintilla (as on Windows), a style for each kind (errors, warnings, the program's, the commands echoed). The
// last 100 commands are kept (Up/Down); "cls" clears the output without going to GDB.
//
class CommandView final : public QWidget, public ISessionObserver {
public:
	explicit CommandView(QWidget* parent = nullptr);
	~CommandView() override;

	QStringList History() const {
		return m_History;
	}
	void SetHistory(const QStringList& history) {
		m_History = history;
		m_HistoryIndex = static_cast<int>(m_History.size());
	}
	void FocusInput();
	void ApplyFont();

private:
	void OnOutput(const DebugEngine::OutputData& data) override;
	void OnSessionChanged() override;
	bool eventFilter(QObject* watched, QEvent* event) override;
	DebugEngine::Task<void> Execute(QString command);
	void Append(const QString& text, int style = 0);

	CommandOutput* m_Output;
	QLineEdit* m_Input;
	QStringList m_History;
	int m_HistoryIndex{ 0 };
	Lifetime m_Lifetime;
};
