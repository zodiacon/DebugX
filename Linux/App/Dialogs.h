#pragma once

#include <QDialog>
#include "IDebugger.h"

class QLineEdit;
class QPlainTextEdit;
class QTreeWidget;
class QCheckBox;
class QComboBox;

// File > Run Executable: the program, its arguments (as a shell splits them) and its working folder
// a program as typed: a path (absolute, relative to the current folder, or ~/...), else a name looked up in PATH, as a
// shell does; empty if there is no such executable
QString ResolveProgram(const QString& typed);

class RunExecutableDialog final : public QDialog {
public:
	explicit RunExecutableDialog(QWidget* parent = nullptr);
	void SetValues(const QString& program, const QString& arguments, const QString& folder);
	QString Program() const;
	QString Arguments() const;
	QString Folder() const;

private:
	QLineEdit* m_Program;
	QLineEdit* m_Arguments;
	QLineEdit* m_Folder;
};

// File > Attach to Process: this machine's processes (/proc), with a filter
class AttachDialog final : public QDialog {
public:
	AttachDialog(const std::vector<DebugEngine::ServerProcess>& processes, QWidget* parent = nullptr);
	ULONG Pid() const;
	QString Name() const;

private:
	QTreeWidget* m_List;
	QLineEdit* m_Filter;
};

// Debug > Add Breakpoint: a code location or memory, with a condition and the rest of BreakpointSettings
class BreakpointDialog final : public QDialog {
public:
	explicit BreakpointDialog(QWidget* parent = nullptr);
	DebugEngine::BreakpointSettings Settings() const;

private:
	QLineEdit* m_Location;
	QLineEdit* m_Condition;
	QLineEdit* m_Command;
	QLineEdit* m_PassCount;
	QCheckBox* m_Data;
	QComboBox* m_Size;
	QComboBox* m_Access;
	QCheckBox* m_OneShot;
};

// Options > Symbol and Source Paths: a folder a line
class PathsDialog final : public QDialog {
public:
	PathsDialog(const QString& symbolPath, const QString& sourcePath, QWidget* parent = nullptr);
	QString SymbolPath() const;
	QString SourcePath() const;

private:
	QPlainTextEdit* m_Symbols;
	QPlainTextEdit* m_Sources;
};
