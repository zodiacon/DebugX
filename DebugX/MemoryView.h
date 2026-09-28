#pragma once

#include <memory>
#include <atltheme.h>		// CBufferedPaintWindowImpl, which CHexControl is built on
#include <SimpleHexControl.h>
#include <MemoryBuffer.h>
#include "Debugger.h"
#include "Lifetime.h"
#include "PaneBase.h"
#include "resource.h"

// A live view of target memory: an address/expression box ("Go", or Enter) reads a fixed-size chunk
// there into a CHexControl (WTLHelper). The expression -- not just the address it last resolved to --
// is what's kept, so a register-based one ("@$csp", say) tracks its target across each stop the way
// CanRefresh's usual "the target just broke" trigger implies it should.
class CMemoryView :
	public CFrameWindowImpl<CMemoryView, CWindow, CControlWinTraits>,
	public CPaneBase<CMemoryView> {
public:
	using BaseFrame = CFrameWindowImpl<CMemoryView, CWindow, CControlWinTraits>;

	CMemoryView() : m_AddressEdit(this, 1) {
	}

	enum { AddressId = 200, GoId };

	BEGIN_MSG_MAP(CMemoryView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		COMMAND_HANDLER(GoId, BN_CLICKED, OnGo)
		CHAIN_MSG_MAP(CPaneBase<CMemoryView>)
		CHAIN_MSG_MAP(BaseFrame)
	ALT_MSG_MAP(1)
		MESSAGE_HANDLER(WM_CHAR, OnAddressChar)
	END_MSG_MAP()

	HWND Create(HWND hParent) override {
		return BaseFrame::Create(hParent, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
	}
	void OnFinalMessage(HWND) override {
		delete this;
	}

	// ISessionObserver
	void OnTargetEnded() override;

	// CPaneBase
	bool CanRefresh() const {
		return !m_Expression.IsEmpty() && CPaneBase<CMemoryView>::CanRefresh();
	}
	DebugEngine::Task<void> Refresh();

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM lParam, BOOL&);
	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGo(WORD, WORD, HWND, BOOL&);
	LRESULT OnAddressChar(UINT, WPARAM wParam, LPARAM, BOOL& handled);

	void Go();

	static constexpr ULONG ChunkSize = 4096;

	CContainedWindowT<CEdit> m_AddressEdit;
	CButton m_Go;
	CHexControl m_Hex;
	std::unique_ptr<MemoryBuffer> m_Buffer;
	CString m_Expression;		// what is loaded: re-evaluated on every refresh, so a register expression follows it
	Lifetime m_Lifetime;
};
