#pragma once

struct IDebugXView {
	virtual HICON GetIcon() const = 0;
	virtual CString GetHeader() const = 0;
	virtual HWND Create(HWND hParent) = 0;
};
