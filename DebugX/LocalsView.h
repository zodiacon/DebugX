#pragma once

#include "SymbolPane.h"
#include "AppMessages.h"
#include "resource.h"

// The arguments and local variables of the scope frame (the call stack's chosen frame; 0 when the target stops).
// Right-click: Add to Watch (a member or element too, as an expression from its variable).
class CLocalsView : public CSymbolPane<CLocalsView> {
public:
	BEGIN_MSG_MAP(CLocalsView)
		CHAIN_MSG_MAP(CSymbolPane<CLocalsView>)
	END_MSG_MAP()

	DebugEngine::Task<HRESULT> Fetch(std::vector<DebugEngine::SymbolEntry>& entries) {
		return DebugManager::DebuggerX().GetLocals(entries, ExpandedPaths());
	}

	bool OnRightClickList(HWND, int row, int, POINT const& pt) {
		if (row < 0 || row >= static_cast<int>(m_Entries.size()))
			return false;
		m_List.SelectItem(row);

		enum { CmdAddWatch = 1, CmdCopyValue };
		CMenu menu;
		menu.CreatePopupMenu();
		menu.AppendMenu(MF_STRING, CmdAddWatch, L"Add to &Watch");
		menu.AppendMenu(MF_STRING, CmdCopyValue, L"&Copy Value");
		switch (menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd)) {
			case CmdAddWatch: AddToWatch(m_hWnd, Expression(row).c_str()); break;
			case CmdCopyValue: CopyText(m_Entries[row].Value); break;
		}
		return true;
	}

	void OnTargetEnded() override {
		m_Expanded.clear();
		SetEntries({});
		MarkDirty();
	}

private:
	// a row as an expression: its variable, then each member (through a pointer: ->) or element down to it
	std::wstring Expression(int row) const {
		std::vector<int> chain{ row };
		for (int i = row - 1; i >= 0 && m_Entries[chain.back()].Depth > 0; i--) {
			if (m_Entries[i].Depth < m_Entries[chain.back()].Depth)
				chain.push_back(i);
		}
		std::wstring expression;
		for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
			auto& e = m_Entries[*it];
			if (expression.empty())
				expression = e.Name;
			else if (e.Name.starts_with(L'['))
				expression += e.Name;
			else {
				auto& parent = m_Entries[*std::prev(it)];
				expression += (parent.Type.ends_with(L'*') ? L"->" : L".") + e.Name;
			}
		}
		return expression;
	}

	void CopyText(const std::wstring& text) {
		if (!OpenClipboard())
			return;
		::EmptyClipboard();
		auto size = (text.size() + 1) * sizeof(wchar_t);
		if (auto hData = ::GlobalAlloc(GMEM_MOVEABLE, size)) {
			memcpy(::GlobalLock(hData), text.c_str(), size);
			::GlobalUnlock(hData);
			if (!::SetClipboardData(CF_UNICODETEXT, hData))
				::GlobalFree(hData);
		}
		::CloseClipboard();
	}
};
