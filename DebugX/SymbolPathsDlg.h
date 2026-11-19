#pragma once

#include "resource.h"
#include "DialogHelper.h"

// Options > Symbol and Source Paths: the engine's symbol path and source path, an entry per line (the engine has them
// ';'-separated, which is what goes in and comes out)
class CSymbolPathsDlg :
	public CDialogImpl<CSymbolPathsDlg>,
	public CDialogHelper<CSymbolPathsDlg> {
public:
	enum { IDD = IDD_SYMBOLPATHS };

	CSymbolPathsDlg(std::wstring symbolPath, std::wstring sourcePath) :
		m_SymbolPath(std::move(symbolPath)), m_SourcePath(std::move(sourcePath)) {}

	BEGIN_MSG_MAP(CSymbolPathsDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
		COMMAND_ID_HANDLER(IDC_SYMSERVER, OnAddSymbolServer)
		COMMAND_ID_HANDLER(IDC_SYMFOLDER, OnAddFolder)
		COMMAND_ID_HANDLER(IDC_SRCFOLDER, OnAddFolder)
	END_MSG_MAP()

	const std::wstring& GetSymbolPath() const {
		return m_SymbolPath;
	}
	const std::wstring& GetSourcePath() const {
		return m_SourcePath;
	}

	// "a;b;;c " -> "a\r\nb\r\nc", and back (blank entries and spaces around them are dropped)
	static std::wstring ToLines(const std::wstring& path);
	static std::wstring FromLines(const std::wstring& lines);

private:
	LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCloseCmd(WORD, WORD wID, HWND, BOOL&);
	LRESULT OnAddSymbolServer(WORD, WORD, HWND, BOOL&);
	LRESULT OnAddFolder(WORD, WORD wID, HWND, BOOL&);

	// adds a line at the end of the edit control, unless it has it already
	void AppendLine(UINT id, const std::wstring& line);

	std::wstring m_SymbolPath, m_SourcePath;
};
