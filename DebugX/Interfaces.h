#pragma once

struct IMainFrame {
	virtual UINT TrackPopupMenu(HMENU hMenu, POINT* pt = nullptr, DWORD flags = 0) = 0;
	virtual CUpdateUIBase* GerUIUpdate() = 0;
};

struct IDebugXView {
	virtual HICON GetIcon() const = 0;
	virtual CString GetHeader() const = 0;
	virtual HWND Create(HWND hParent) = 0;
	virtual bool IsSingle() const {
		return true;
	}
	virtual bool CanClose() const {
		return true;
	}
	virtual bool OnTabContextMenu(IMainFrame* frame) {
		return false;
	}
	virtual void OnActivate(bool activate) {}
};
