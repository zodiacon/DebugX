#pragma once

#include <QPlainTextEdit>
#include "SessionModel.h"

//
// Output: the session's event log (SessionModel::Log) -- processes, threads and modules coming and going, exceptions,
// the program's output a line each. The log is the model's, so the pane is complete whenever it is made.
//
class OutputView final : public QPlainTextEdit, public ISessionObserver {
public:
	explicit OutputView(QWidget* parent = nullptr);
	~OutputView() override;

private:
	void OnLogLine(const std::wstring& line) override;
	void contextMenuEvent(QContextMenuEvent* event) override;
};
