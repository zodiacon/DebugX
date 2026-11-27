#pragma once

#include <map>
#include <QTreeWidget>
#include "PaneBase.h"

class QLineEdit;

//
// A pane that is a list (CListPane on Windows): a QTreeWidget without a tree, an optional filter box above it (Ctrl+F;
// rows whose cells lack the text are hidden; Escape empties it), Ctrl+C copies the selected rows tab-separated (with a
// header line when there are several), and the column widths are kept (SaveState/RestoreState).
//
class ListPane : public PaneBase {
public:
	ListPane(const QStringList& headers, bool filter, QWidget* parent = nullptr);

	QByteArray SaveState() const;
	void RestoreState(const QByteArray& state);

protected:
	// a row of texts, at the end
	QTreeWidgetItem* AddRow(const QStringList& texts);
	// remembers the selection and scroll position, clears; EndUpdate puts them back and applies the filter
	void BeginUpdate();
	void EndUpdate();
	void ApplyFilter();
	void CopySelection(bool all = false);
	// a context menu's Copy / Copy All, at its end
	void AddCopyItems(class QMenu& menu);
	bool eventFilter(QObject* watched, QEvent* event) override;

	QTreeWidget* m_List;
	QLineEdit* m_Filter{ nullptr };

private:
	int m_SavedRow{ -1 };
	int m_SavedScroll{ 0 };
};

class CallStackView final : public ListPane {
public:
	explicit CallStackView(QWidget* parent = nullptr);

private:
	DebugEngine::Task<void> Refresh() override;
	void Clear() override;
	DebugEngine::Task<void> SwitchTo(ULONG frame);
	std::vector<DebugEngine::StackFrameInfo> m_Frames;
};

class RegistersView final : public ListPane {
public:
	explicit RegistersView(QWidget* parent = nullptr);

private:
	DebugEngine::Task<void> Refresh() override;
	void Clear() override;
	DebugEngine::Task<void> Edit(QString name, QString value);
	std::map<QString, QString> m_Previous;
	ULONG m_PreviousThread{ 0 }, m_PreviousFrame{ ~0u };
};

class ModulesView final : public ListPane {
public:
	explicit ModulesView(QWidget* parent = nullptr);

private:
	DebugEngine::Task<void> Refresh() override;
	void Clear() override;
	void OnModuleLoaded(const DebugEngine::ModuleEventData&) override { MarkDirty(); }
	void OnModuleUnloaded(const DebugEngine::ModuleEventData&) override { MarkDirty(); }
};

class ThreadsView final : public ListPane {
public:
	explicit ThreadsView(QWidget* parent = nullptr);

private:
	DebugEngine::Task<void> Refresh() override;
	void Clear() override;
	void OnThreadCreated(const DebugEngine::ThreadCreateData&) override { MarkDirty(); }
	void OnThreadExited(const DebugEngine::ExitThreadData&) override { MarkDirty(); }
};

class BreakpointsView final : public ListPane {
public:
	explicit BreakpointsView(QWidget* parent = nullptr);

private:
	DebugEngine::Task<void> Refresh() override;
	bool CanRefresh() const override;
	void OnBreakpointsChanged() override { MarkDirty(); }
	void OnTargetEnded() override { MarkDirty(); }
	DebugEngine::Task<void> SetEnabled(ULONG id, bool enabled);
	DebugEngine::Task<void> EditCondition(ULONG id);
	DebugEngine::Task<void> DeleteSelected();
	std::vector<DebugEngine::BreakpointInfo> m_Breakpoints;
	bool m_Building{ false };
};
