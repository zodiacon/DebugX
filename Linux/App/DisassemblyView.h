#pragma once

#include <map>
#include <QStringList>
#include "PaneBase.h"

class CodeEditor;
class QLineEdit;

//
// Disassembly (CDisassemblyView): the code around the scope frame's instruction, each source line before its
// instructions (lines that start with ';'), breakpoint and current-instruction markers. The address bar shows code from
// an address, a symbol or any expression until the next stop, step, thread or frame change.
//
class DisassemblyView final : public PaneBase {
public:
	explicit DisassemblyView(QWidget* parent = nullptr);

	// shows code from there (empty: the current instruction again)
	void GoToAddress(const QString& expression);

	bool ToggleBreakpoint() override;
	bool RunToCursor() override;
	bool SetNextStatement() override;
	bool GoTo() override;

	bool ShowSource() const {
		return m_ShowSource;
	}
	void SetShowSource(bool show);

private:
	DebugEngine::Task<void> Refresh() override;
	void Clear() override;
	void OnBroke() override;
	void OnContextChanged() override;
	void OnScopeChanged() override;
	void OnBreakpointsChanged() override { MarkDirty(); }
	DebugEngine::Task<void> Evaluate(QString expression);
	DebugEngine::Task<void> ToggleAt(ULONG64 address);
	ULONG64 CaretAddress() const;
	QString SourceLine(const QString& path, ULONG line);

	QLineEdit* m_Address;
	CodeEditor* m_Text;
	std::vector<ULONG64> m_LineAddresses;	// by line; 0 for a source line
	ULONG64 m_From{ 0 };					// from the address bar; 0: the current instruction
	bool m_ShowSource{ true };
	std::map<QString, QStringList> m_Files;
};
