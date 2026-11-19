#include "stdafx.h"
#include "WatchView.h"

using namespace DebugEngine;

namespace {
	// the separator of the expressions in the saved state (one line of it: no newlines), and what they come after
	constexpr wchar_t Separator = L'\t';
	constexpr PCWSTR ExpressionsTag = L"|e:";

	enum { CmdAdd = 1, CmdEdit, CmdRemove, CmdRemoveAll };
}

void CWatchView::InitList() {
	CSymbolPane<CWatchView>::InitList();
	m_List.ModifyStyle(0, LVS_EDITLABELS);
	ShowUnevaluated();
}

CString CWatchView::GetColumnText(HWND hWnd, int row, int column) const {
	if (IsNewRow(row))
		return column == 0 ? L"(double-click to add)" : L"";
	return CSymbolPane<CWatchView>::GetColumnText(hWnd, row, column);
}

int CWatchView::ExpressionIndex(int row) const {
	if (row < 0 || row >= static_cast<int>(m_Entries.size()) || m_Entries[row].Depth != 0)
		return -1;

	// each expression is a single root, in order
	int index = 0;
	for (int i = 0; i < row; i++) {
		if (m_Entries[i].Depth == 0)
			index++;
	}
	return index < static_cast<int>(m_Expressions.size()) ? index : -1;
}

bool CWatchView::OnDoubleClickList(HWND hWnd, int row, int col, POINT const& pt) {
	if (IsNewRow(row) || (ExpressionIndex(row) >= 0 && !m_Entries[row].Expandable)) {
		Edit(row);
		return true;
	}
	return CSymbolPane<CWatchView>::OnDoubleClickList(hWnd, row, col, pt);
}

bool CWatchView::OnRightClickList(HWND, int row, int, POINT const& pt) {
	if (row >= 0)
		m_List.SelectItem(row);

	const bool isExpression = ExpressionIndex(row) >= 0;
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, CmdAdd, L"&Add Watch\tIns");
	menu.AppendMenu(isExpression ? MF_STRING : MF_GRAYED, CmdEdit, L"&Edit\tF2");
	menu.AppendMenu(isExpression ? MF_STRING : MF_GRAYED, CmdRemove, L"&Remove\tDel");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(m_Expressions.empty() ? MF_GRAYED : MF_STRING, CmdRemoveAll, L"Remove A&ll");
	switch (menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd)) {
		case CmdAdd: Edit(static_cast<int>(m_Entries.size())); break;
		case CmdEdit: Edit(row); break;
		case CmdRemove: Remove(row); break;
		case CmdRemoveAll:
			m_Expressions.clear();
			m_Expanded.clear();
			ExpressionsChanged();
			break;
	}
	return true;
}

bool CWatchView::OnKey(UINT vk, int row) {
	switch (vk) {
		case VK_INSERT: Edit(static_cast<int>(m_Entries.size())); return true;
		case VK_F2: Edit(row); return true;
		case VK_DELETE: Remove(row); return true;
		case VK_RETURN:
			if (IsNewRow(row)) {
				Edit(row);
				return true;
			}
			break;
	}
	return false;
}

void CWatchView::Edit(int row) {
	if (!IsNewRow(row) && ExpressionIndex(row) < 0)
		return;

	m_List.SetFocus();
	m_List.EnsureVisible(row, FALSE);
	m_List.EditLabel(row);
}

void CWatchView::Remove(int row) {
	auto index = ExpressionIndex(row);
	if (index < 0)
		return;

	m_Expressions.erase(m_Expressions.begin() + index);
	ExpressionsChanged();
}

LRESULT CWatchView::OnBeginLabelEdit(int, LPNMHDR hdr, BOOL& handled) {
	if (hdr->hwndFrom != m_List.m_hWnd) {
		handled = FALSE;
		return 0;
	}

	const int row = reinterpret_cast<NMLVDISPINFO*>(hdr)->item.iItem;
	const int index = ExpressionIndex(row);
	if (!IsNewRow(row) && index < 0)
		return TRUE;		// a member of an expression: not editable

	m_EditRow = row;
	// (what the list put in the box is the row's text: indented, with the triangle)
	m_List.GetEditControl().SetWindowText(index >= 0 ? m_Expressions[index].c_str() : L"");
	return FALSE;
}

LRESULT CWatchView::OnEndLabelEdit(int, LPNMHDR hdr, BOOL& handled) {
	if (hdr->hwndFrom != m_List.m_hWnd) {
		handled = FALSE;
		return 0;
	}

	auto& item = reinterpret_cast<NMLVDISPINFO*>(hdr)->item;
	const int row = m_EditRow;
	m_EditRow = -1;
	if (item.pszText == nullptr)		// cancelled
		return FALSE;

	CString text(item.pszText);
	text.Trim();
	text.Remove(Separator);
	if (IsNewRow(row)) {
		if (!text.IsEmpty()) {
			m_Expressions.push_back((PCWSTR)text);
			ExpressionsChanged();
		}
	}
	else if (auto index = ExpressionIndex(row); index >= 0) {
		if (text.IsEmpty())
			m_Expressions.erase(m_Expressions.begin() + index);
		else
			m_Expressions[index] = (PCWSTR)text;
		ExpressionsChanged();
	}
	// (the rows are the pane's, not the list's: it does not keep the text)
	return FALSE;
}

LRESULT CWatchView::OnAddWatch(UINT, WPARAM, LPARAM lParam, BOOL&) {
	CString text(reinterpret_cast<PCWSTR>(lParam));
	text.Trim();
	text.Remove(Separator);
	if (text.IsEmpty())
		return 0;

	m_Expressions.push_back((PCWSTR)text);
	ExpressionsChanged();
	// (its row: the last before the one for a new expression, once it is evaluated -- shown now, unevaluated or not)
	m_List.SetItemState(-1, 0, LVIS_SELECTED);
	m_List.EnsureVisible(m_List.GetItemCount() - 1, FALSE);
	return 0;
}

void CWatchView::ExpressionsChanged() {
	if (CanRefresh())
		RequestRefresh();
	else
		ShowUnevaluated();
}

void CWatchView::ShowUnevaluated() {
	std::vector<SymbolEntry> entries;
	entries.reserve(m_Expressions.size());
	for (auto& expr : m_Expressions) {
		SymbolEntry e;
		e.Path = e.Name = expr;
		entries.push_back(std::move(e));
	}
	SetEntries(std::move(entries));
	// (and evaluated when the target next stops)
	MarkDirty();
}

void CWatchView::OnTargetEnded() {
	ShowUnevaluated();
}

std::wstring CWatchView::SaveState() const {
	auto state = ColumnState::SaveColumnWidths(m_List);
	state += ExpressionsTag;
	for (size_t i = 0; i < m_Expressions.size(); i++) {
		if (i > 0)
			state += Separator;
		state += m_Expressions[i];
	}
	return state;
}

void CWatchView::LoadState(PCWSTR state) {
	// (the column widths stop at the tag)
	ColumnState::LoadColumnWidths(m_List, state);

	std::wstring_view text(state ? state : L"");
	auto pos = text.find(ExpressionsTag);
	if (pos == std::wstring_view::npos)
		return;

	m_Expressions.clear();
	text.remove_prefix(pos + wcslen(ExpressionsTag));
	while (!text.empty()) {
		auto end = text.find(Separator);
		auto expr = text.substr(0, end);
		if (!expr.empty())
			m_Expressions.emplace_back(expr);
		if (end == std::wstring_view::npos)
			break;
		text.remove_prefix(end + 1);
	}
	ExpressionsChanged();
}
