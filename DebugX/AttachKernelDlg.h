#pragma once

#include "resource.h"
#include "DialogHelper.h"
#include <functional>
#include <memory>

// A kernel debugging connection, as windbg -k (and IDebugClient::AttachKernel) takes it, in its parts:
//   net:port=<port>,key=<w.x.y.z>[,target=<host>]
//   com:port=<COMn>,baud=<rate>      com:pipe,port=\\.\pipe\<name>[,resets=<n>][,reconnect]
//   usb:targetname=<name>
// anything else (other options, other transports) is Custom: the string as it is
struct KernelConnection {
	enum class Kind { Net, Com, Usb, Custom };

	Kind Type{ Kind::Net };
	CString NetPort{ L"50000" }, NetKey, NetTarget;
	bool ComPipe{ false };
	CString ComPort{ L"COM1" }, ComBaud{ L"115200" }, PipeName{ L"\\\\.\\pipe\\com_1" }, Resets{ L"0" };
	bool Reconnect{ true };
	CString UsbTarget;
	CString Custom;

	static KernelConnection Parse(PCWSTR text);
};

// a tab's page of the dialog
struct IKernelPage {
	virtual ~IKernelPage() = default;
	virtual HWND Window() = 0;
	virtual void Load(KernelConnection const& conn) = 0;
	// what is wrong with the page's settings, empty if nothing is
	virtual CString Validate() = 0;
	// the connection string (when Validate has nothing to say)
	virtual CString Format() = 0;
	// what to run on the target to set it up for this connection, empty if nothing to say
	virtual CString Setup() { return L""; }
	virtual void Destroy() = 0;
};

template<typename T>
class CKernelPage : public CDialogImpl<T>, public IKernelPage {
public:
	std::function<void()> Changed;

	HWND Window() override {
		return this->m_hWnd;
	}
	void Destroy() override {
		if (this->IsWindow())
			this->DestroyWindow();
	}

	BEGIN_MSG_MAP(CKernelPage)
		MESSAGE_HANDLER(WM_COMMAND, OnCommand)
	END_MSG_MAP()

protected:
	CString GetText(UINT id) const {
		CString text;
		this->GetDlgItemText(id, text);
		return text.Trim();
	}

	// a control of the page changed
	virtual void OnChanged() {}

private:
	LRESULT OnCommand(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
		bHandled = FALSE;
		// (the dialog sets the setup command's text: not a change of the settings)
		if (LOWORD(wParam) == IDC_KERNEL_SETUP)
			return 0;

		switch (HIWORD(wParam)) {
			case CBN_SELCHANGE:
			{
				// the combo box's text is still the old one while this notification is handled
				CComboBox cb((HWND)lParam);
				if (int sel = cb.GetCurSel(); sel >= 0) {
					CString text;
					cb.GetLBText(sel, text);
					cb.SetWindowText(text);
				}
				[[fallthrough]];
			}
			case EN_CHANGE:
			case CBN_EDITCHANGE:
			case BN_CLICKED:
				OnChanged();
				if (Changed)
					Changed();
				break;
		}
		bHandled = FALSE;
		return 0;
	}
};

class CNetKernelPage : public CKernelPage<CNetKernelPage> {
public:
	enum { IDD = IDD_KERNEL_NET };

	BEGIN_MSG_MAP(CNetKernelPage)
		COMMAND_ID_HANDLER(IDC_NET_GENKEY, OnGenerateKey)
		CHAIN_MSG_MAP(CKernelPage<CNetKernelPage>)
	END_MSG_MAP()

	void Load(KernelConnection const& conn) override;
	CString Validate() override;
	CString Format() override;
	CString Setup() override;

private:
	LRESULT OnGenerateKey(WORD, WORD, HWND, BOOL& bHandled);
	CString m_HostAddress;
};

class CComKernelPage : public CKernelPage<CComKernelPage> {
public:
	enum { IDD = IDD_KERNEL_COM };

	void Load(KernelConnection const& conn) override;
	CString Validate() override;
	CString Format() override;
	CString Setup() override;

private:
	void OnChanged() override;
};

class CUsbKernelPage : public CKernelPage<CUsbKernelPage> {
public:
	enum { IDD = IDD_KERNEL_USB };

	void Load(KernelConnection const& conn) override;
	CString Validate() override;
	CString Format() override;
	CString Setup() override;
};

class CCustomKernelPage : public CKernelPage<CCustomKernelPage> {
public:
	enum { IDD = IDD_KERNEL_CUSTOM };

	void Load(KernelConnection const& conn) override;
	CString Validate() override;
	CString Format() override;
	void SetText(CString const& text);
};

// asks for the connection of a kernel debugging session: a tab for each way to connect (network, serial port or a
// virtual machine's pipe, USB, a connection string as windbg -k takes it), each with its settings
class CAttachKernelDlg :
	public CDialogImpl<CAttachKernelDlg>,
	public CDialogHelper<CAttachKernelDlg> {
public:
	enum { IDD = IDD_ATTACHKERNEL };

	explicit CAttachKernelDlg(PCWSTR connection = nullptr) : m_Connection(connection) {}

	BEGIN_MSG_MAP(CAttachKernelDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
		NOTIFY_HANDLER(IDC_KERNEL_TABS, TCN_SELCHANGE, OnTabChanged)
	END_MSG_MAP()

	const CString& GetConnection() const {
		return m_Connection;
	}

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnTabChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

	template<typename T>
	void AddPage(PCWSTR title, KernelConnection const& conn);
	void ShowPage(int index);
	void UpdateConnection();

	CTabCtrl m_Tabs;
	std::vector<std::unique_ptr<IKernelPage>> m_Pages;
	CCustomKernelPage* m_CustomPage{ nullptr };
	int m_Current{ -1 };
	CString m_Connection;
	bool m_Ready{ false };
};
