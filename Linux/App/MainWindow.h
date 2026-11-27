#pragma once

#include <map>
#include <QMainWindow>
#include "Common.h"
#include "SessionModel.h"

class QAction;
class QDockWidget;
class QLabel;
class QMenu;
class QTabWidget;
class CommandView;
class OutputView;
class CallStackView;
class RegistersView;
class ModulesView;
class ThreadsView;
class BreakpointsView;
class LocalsView;
class WatchView;
class DisassemblyView;
class MemoryView;
class SourceView;
class PaneBase;
class ListPane;

//
// The main window (CMainFrame): the documents (the Command view and the source files) in the middle, the panes docked
// around them (QDockWidget; the arrangement is saved and restored), the menus and toolbar whose state follows the session
// (UpdateUI, from OnSessionChanged), and what ties the panes together: where the target is in the source, the breakpoint
// markers, the breakpoints saved for each program, the recent targets, the command line.
//
class MainWindow final : public QMainWindow, public ISessionObserver {
public:
	MainWindow();
	~MainWindow() override;

	static MainWindow& Get() {
		return *s_Instance;
	}

	// debugx [program [arguments...]] | -p pid | -pn name | -z core
	void StartFromCommandLine(const QStringList& arguments);

	// for the panes
	void RunToAddress(ULONG64 address);
	void SetNextStatement(ULONG64 address);
	void GoToDisassembly(const QString& expression);
	void ShowAddressSource(ULONG64 address);
	void AddBreakpointDialog();
	void SetAllBreakpointsEnabled(bool enabled);
	void DeleteAllBreakpoints();
	void AddWatch(const QString& expression);
	void ToggleSourceBreakpoint(SourceView* view, int line);
	void RunToSourceLine(SourceView* view, int line);
	void SetNextStatementToLine(SourceView* view, int line);
	void SetStatusMessage(const QString& text);

protected:
	void closeEvent(QCloseEvent* event) override;
	void dragEnterEvent(QDragEnterEvent* event) override;
	void dropEvent(QDropEvent* event) override;

private:
	struct LaunchInfo {
		QString Program, Arguments, Folder;
	};

	// ISessionObserver
	void OnSessionChanged() override;
	void OnBroke() override;
	void OnContextChanged() override;
	void OnScopeChanged() override;
	void OnBreakpointsChanged() override;
	void OnTargetEnded() override;
	void OnException(const DebugEngine::ExceptionData& data) override;

	void CreatePanes();
	void CreateActions();
	void DefaultLayout();
	QDockWidget* AddDock(QWidget* widget, const QString& title, const QString& name, QKeySequence shortcut = {});
	void UpdateUI();
	void UpdateTitle();
	void ApplySettings();
	void SaveSettings();
	void ApplyFont();

	// sessions
	void RunExecutable();
	void AttachToProcess();
	void OpenDump();
	DebugEngine::Task<void> Launch(LaunchInfo info);
	DebugEngine::Task<void> Attach(ULONG pid, QString name);
	DebugEngine::Task<void> OpenDumpFile(QString path);
	DebugEngine::Task<void> Restart();
	DebugEngine::Task<void> Go();
	DebugEngine::Task<void> Step(DebugEngine::StepKind kind);
	DebugEngine::Task<void> SetNextStatementTask(ULONG64 address);
	DebugEngine::Task<void> SaveDump();
	DebugEngine::Task<void> ShowNextStatement();
	DebugEngine::Task<void> EditPaths();
	void ReportFailure(const QString& what, HRESULT hr);

	// recent targets (File > Recent Targets)
	void AddRecent(const QStringList& entry);
	void BuildRecentMenu();
	DebugEngine::Task<void> OpenRecent(QStringList entry);

	// breakpoints, kept for each program (by its executable's path)
	DebugEngine::Task<void> SaveBreakpoints();
	DebugEngine::Task<void> RestoreBreakpoints(QString program);
	DebugEngine::Task<void> UpdateBreakpointMarkers();
	DebugEngine::Task<void> ToggleSourceBreakpointTask(QString path, QString file, int line);

	// source documents
	SourceView* OpenSource(const QString& path, bool activate);
	DebugEngine::Task<void> UpdateSourceLocation();
	DebugEngine::Task<void> ShowAddressSourceTask(ULONG64 address);

	// what the commands that act on "the active pane" act on
	PaneBase* ActivePane() const;
	SourceView* ActiveSource() const;
	void Find(bool next, bool backward);

	static inline MainWindow* s_Instance;
	QTabWidget* m_Documents;
	CommandView* m_Command;
	OutputView* m_Output;
	CallStackView* m_CallStack;
	RegistersView* m_Registers;
	ModulesView* m_Modules;
	ThreadsView* m_Threads;
	BreakpointsView* m_Breakpoints;
	LocalsView* m_Locals;
	WatchView* m_Watch;
	DisassemblyView* m_Disassembly;
	MemoryView* m_Memory;
	std::vector<std::pair<ListPane*, QString>> m_ListPanes;
	std::map<QString, SourceView*> m_Sources;
	QByteArray m_DefaultState;

	QMenu* m_RecentMenu;
	QMenu* m_LayoutsMenu;
	std::map<QString, QAction*> m_Actions;
	QLabel* m_StatusMessage;
	QLabel* m_StatusState;
	QLabel* m_StatusTarget;
	QLabel* m_StatusEngine;

	LaunchInfo m_Launch;			// the last Run Executable (Restart)
	QString m_TargetName;
	QString m_BreakpointsKey;		// the program whose breakpoints are being kept
	QString m_LastProgram;
	QString m_FindText;
	std::uint64_t m_LocationGeneration{ 0 };
	Lifetime m_Lifetime;
};
