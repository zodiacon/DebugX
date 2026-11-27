#pragma once

#include <set>
#include <Qsci/qsciscintilla.h>
#include "Common.h"

class QsciLexer;

//
// The read-only Scintilla (QScintilla) behind the source documents and the Disassembly pane, as CSourceView's and
// CDisassemblyView's on Windows: a lexer (C++, or the asm lexer with x86-64's instructions and registers), line numbers
// (optional), and a symbol margin with breakpoints (red circles; hollow: disabled), the current thread's line (a yellow
// arrow and background) and the scope frame's (green) -- a click on it toggles a breakpoint at the line.
//
class CodeEditor : public QsciScintilla {
	Q_OBJECT
public:
	enum class Language { Text, Cpp, Asm };
	explicit CodeEditor(Language language, QWidget* parent = nullptr);

	void SetShowLineNumbers(bool show);
	// lines are 0-based; -1: none
	void SetBreakpointLines(std::set<int> lines);
	void SetDisabledBreakpointLines(std::set<int> lines);
	void SetCurrentLine(int line, bool scope);
	int CurrentLine() const {
		return m_CurrentLine;
	}
	// puts the line in view (centered if it is not) and the caret on it
	void ShowLine(int line);
	int CaretLine() const;
	// the line at a point of the viewport (-1: none)
	int LineAt(const QPoint& pos) const;
	// the code font, and the colors from the palette (light or dark)
	void ApplyFont();
	void ApplyColors();

signals:
	void GutterClicked(int line);

protected:
	void changeEvent(QEvent* event) override;

private:
	enum { MarginNumbers = 0, MarginSymbols = 1 };
	enum { MarkerBreakpoint, MarkerBreakpointDisabled, MarkerCurrent, MarkerFrame, MarkerCurrentLine, MarkerFrameLine };

	void UpdateMarkers();
	void UpdateNumberWidth();

	QsciLexer* m_Lexer{ nullptr };
	Language m_Language;
	bool m_ShowLineNumbers{ true };
	std::set<int> m_Breakpoints, m_DisabledBreakpoints;
	int m_CurrentLine{ -1 };
	bool m_CurrentIsScope{ false };
};

// whether the palette is dark (the colors follow it)
bool IsDarkPalette();

//
// A source file, as a document tab (opened by the main window when the target stops in it, or by the user)
//
class SourceView final : public CodeEditor {
	Q_OBJECT
public:
	SourceView(const QString& path, QWidget* parent = nullptr);

	const QString& Path() const {
		return m_Path;
	}
	bool Load();
	// the name the symbols have for the file, for breakpoints (else the path)
	void SetSymbolFile(const QString& file) {
		m_SymbolFile = file;
	}
	QString LocationFile() const {
		return m_SymbolFile.isEmpty() ? m_Path : m_SymbolFile;
	}

protected:
	void contextMenuEvent(QContextMenuEvent* event) override;

private:
	DebugEngine::Task<void> ShowValueTip(QString expression, QPoint where);
	// what the mouse rests on (a byte position in the text): the selection if it is in it, else a name with what it is
	// a member of (a.b, p->b, this->b)
	QString ExpressionAt(int position) const;

	QString m_Path, m_SymbolFile;
	Lifetime m_Lifetime;
};
