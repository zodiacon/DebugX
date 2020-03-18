#pragma once

struct IDebugXView {
	virtual HICON GetIcon() const = 0;
	virtual CString GetHeader() const = 0;
	virtual DWORD GetWindowStyle() const = 0;
};
