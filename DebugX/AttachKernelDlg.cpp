#include "pch.h"
#include "AttachKernelDlg.h"
#include <random>
#include <algorithm>
#include <iphlpapi.h>
#include <Uxtheme.h>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace {
	// a connection string's "name=value" and "flag" options, after the transport and its ':'
	struct Option {
		CString Name, Value;
		bool HasValue;
	};

	std::vector<Option> SplitOptions(CString const& text) {
		std::vector<Option> options;
		int start = 0;
		while (start <= text.GetLength()) {
			int comma = text.Find(L',', start);
			if (comma < 0)
				comma = text.GetLength();
			CString item = text.Mid(start, comma - start).Trim();
			if (!item.IsEmpty()) {
				Option option;
				int eq = item.Find(L'=');
				option.HasValue = eq >= 0;
				option.Name = (eq >= 0 ? item.Left(eq) : item).Trim().MakeLower();
				option.Value = eq >= 0 ? item.Mid(eq + 1).Trim() : CString();
				options.push_back(option);
			}
			start = comma + 1;
		}
		return options;
	}

	bool IsNumber(CString const& text, unsigned long long min, unsigned long long max) {
		if (text.IsEmpty() || text.GetLength() > 10 || text.SpanIncluding(L"0123456789") != text)
			return false;
		auto value = _wcstoui64(text, nullptr, 10);
		return value >= min && value <= max;
	}

	// what can go in a value of the connection string as it is (no separators)
	bool IsPlainValue(CString const& text) {
		return !text.IsEmpty() && text.FindOneOf(L", \t=") < 0;
	}

	// a KDNET key: four words of letters and digits (base 36, each at most 64 bits: 13 characters), dot-separated
	bool IsNetKey(CString const& key) {
		int words = 0, start = 0;
		for (;;) {
			int dot = key.Find(L'.', start);
			CString word = dot < 0 ? key.Mid(start) : key.Mid(start, dot - start);
			if (word.IsEmpty() || word.GetLength() > 13)
				return false;
			for (int i = 0; i < word.GetLength(); i++)
				if (!iswascii(word[i]) || !iswalnum(word[i]))
					return false;
			words++;
			if (dot < 0)
				break;
			start = dot + 1;
		}
		return words == 4;
	}

	// the IPv4 address of this machine on a network that has a gateway (the one the target is likely to reach it on)
	CString GetHostAddress() {
		ULONG size = 16 << 10;
		std::unique_ptr<BYTE[]> buffer;
		ULONG error;
		for (int i = 0; i < 3; i++) {
			buffer = std::make_unique<BYTE[]>(size);
			error = ::GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
				nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.get()), &size);
			if (error != ERROR_BUFFER_OVERFLOW)
				break;
		}
		if (error != ERROR_SUCCESS)
			return L"";

		CString fallback;
		for (auto adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.get()); adapter; adapter = adapter->Next) {
			if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
				continue;
			for (auto addr = adapter->FirstUnicastAddress; addr; addr = addr->Next) {
				if (addr->Address.lpSockaddr->sa_family != AF_INET)
					continue;
				auto& b = reinterpret_cast<sockaddr_in*>(addr->Address.lpSockaddr)->sin_addr.S_un.S_un_b;
				CString text;
				text.Format(L"%u.%u.%u.%u", b.s_b1, b.s_b2, b.s_b3, b.s_b4);
				if (adapter->FirstGatewayAddress)
					return text;
				if (fallback.IsEmpty())
					fallback = text;
			}
		}
		return fallback;
	}

	// a drop-down combo box's text: its item, selected, if it has one (the list opens on it), else the text alone
	void SetComboText(CComboBox& cb, CString const& text) {
		int index = cb.FindStringExact(-1, text);
		if (index >= 0)
			cb.SetCurSel(index);
		else
			cb.SetWindowText(text);
	}

	// (only when it changes: no notifications, no flicker)
	void SetText(HWND hDlg, int id, CString const& text) {
		CWindow ctrl(::GetDlgItem(hDlg, id));
		if (!ctrl)		// (a page without it)
			return;
		CString current;
		ctrl.GetWindowText(current);
		if (current != text)
			::SetDlgItemText(hDlg, id, text);
	}

	// COM1, COM2, ..., as HARDWARE\DEVICEMAP\SERIALCOMM has them, in their numbers' order
	std::vector<CString> GetSerialPorts() {
		std::vector<CString> ports;
		CRegKey key;
		if (key.Open(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM", KEY_READ) == ERROR_SUCCESS) {
			for (DWORD i = 0; ; i++) {
				WCHAR name[256], data[64];
				DWORD nameLen = _countof(name), dataSize = sizeof(data) - sizeof(WCHAR), type;
				auto error = ::RegEnumValue(key, i, name, &nameLen, nullptr, &type, reinterpret_cast<BYTE*>(data), &dataSize);
				if (error == ERROR_NO_MORE_ITEMS)
					break;
				if (error != ERROR_SUCCESS || type != REG_SZ)
					continue;
				data[dataSize / sizeof(WCHAR)] = 0;
				ports.push_back(data);
			}
		}
		if (ports.empty())
			ports = { L"COM1", L"COM2", L"COM3", L"COM4" };

		std::sort(ports.begin(), ports.end(), [](auto const& a, auto const& b) {
			auto na = _wtoi(a.Mid(3)), nb = _wtoi(b.Mid(3));
			return na != nb ? na < nb : a.CompareNoCase(b) < 0;
			});
		ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
		return ports;
	}
}

KernelConnection KernelConnection::Parse(PCWSTR text) {
	KernelConnection conn;
	CString str(text);
	str.Trim();
	conn.Custom = str;
	if (str.IsEmpty())
		return conn;

	int colon = str.Find(L':');
	CString transport = (colon < 0 ? str : str.Left(colon)).Trim().MakeLower();
	auto options = SplitOptions(colon < 0 ? CString() : str.Mid(colon + 1));
	bool known = true;

	if (transport == L"net") {
		for (auto& o : options) {
			if (o.Name == L"port" && o.HasValue)
				conn.NetPort = o.Value;
			else if (o.Name == L"key" && o.HasValue)
				conn.NetKey = o.Value;
			else if (o.Name == L"target" && o.HasValue)
				conn.NetTarget = o.Value;
			else
				known = false;
		}
		conn.Type = Kind::Net;
	}
	else if (transport == L"com") {
		CString port;
		conn.Reconnect = false;
		conn.Resets.Empty();
		for (auto& o : options) {
			if (o.Name == L"port" && o.HasValue)
				port = o.Value;
			else if (o.Name == L"baud" && o.HasValue)
				conn.ComBaud = o.Value;
			else if (o.Name == L"pipe" && !o.HasValue)
				conn.ComPipe = true;
			else if (o.Name == L"resets" && o.HasValue)
				conn.Resets = o.Value;
			else if (o.Name == L"reconnect" && !o.HasValue)
				conn.Reconnect = true;
			else
				known = false;
		}
		if (conn.ComPipe) {
			if (!port.IsEmpty())
				conn.PipeName = port;
		}
		else {
			// (pipe-only options on a serial port: shown as they are)
			if (conn.Reconnect || !conn.Resets.IsEmpty())
				known = false;
			if (!port.IsEmpty())
				conn.ComPort = port;
			conn.Resets = L"0";
			conn.Reconnect = true;
		}
		conn.Type = Kind::Com;
	}
	else if (transport == L"usb") {
		for (auto& o : options) {
			if (o.Name == L"targetname" && o.HasValue)
				conn.UsbTarget = o.Value;
			else
				known = false;
		}
		conn.Type = Kind::Usb;
	}
	else {
		known = false;
	}

	if (!known)
		conn.Type = Kind::Custom;
	return conn;
}

//
// Network
//

void CNetKernelPage::Load(KernelConnection const& conn) {
	m_HostAddress = GetHostAddress();
	CEdit(GetDlgItem(IDC_NET_PORT)).SetLimitText(5);
	SetDlgItemText(IDC_NET_PORT, conn.NetPort);
	SetDlgItemText(IDC_NET_KEY, conn.NetKey);
	SetDlgItemText(IDC_NET_TARGET, conn.NetTarget);
}

CString CNetKernelPage::Validate() {
	if (!IsNumber(GetText(IDC_NET_PORT), 49152, 65535))
		return L"The port must be a number from 49152 to 65535.";
	auto key = GetText(IDC_NET_KEY);
	if (key.IsEmpty())
		return L"Enter the key (the one bcdedit /dbgsettings net set on the target), or generate one.";
	if (!IsNetKey(key))
		return L"The key is four words of letters and digits, separated by dots (e.g. 1a2b.3c4d.5e6f.7g8h).";
	auto target = GetText(IDC_NET_TARGET);
	if (!target.IsEmpty() && !IsPlainValue(target))
		return L"The target is a host name or an IP address.";
	return L"";
}

CString CNetKernelPage::Format() {
	CString text;
	text.Format(L"net:port=%s,key=%s", (PCWSTR)GetText(IDC_NET_PORT), (PCWSTR)GetText(IDC_NET_KEY));
	if (auto target = GetText(IDC_NET_TARGET); !target.IsEmpty())
		text += L",target=" + target;
	return text;
}

CString CNetKernelPage::Setup() {
	CString port = GetText(IDC_NET_PORT), key = GetText(IDC_NET_KEY);
	CString text;
	text.Format(L"bcdedit /dbgsettings net hostip:%s port:%s%s%s && bcdedit /debug on",
		m_HostAddress.IsEmpty() ? L"<this computer's IP address>" : (PCWSTR)m_HostAddress,
		port.IsEmpty() ? L"50000" : (PCWSTR)port, key.IsEmpty() ? L"" : L" key:", (PCWSTR)key);
	return text;
}

LRESULT CNetKernelPage::OnGenerateKey(WORD, WORD, HWND, BOOL&) {
	// four random words, as bcdedit makes them when it is given no key
	static const WCHAR digits[] = L"0123456789abcdefghijklmnopqrstuvwxyz";
	std::random_device rd;
	std::uniform_int_distribution<int> dist(0, 35);
	CString key;
	for (int w = 0; w < 4; w++) {
		if (w)
			key += L'.';
		for (int i = 0; i < 8; i++)
			key += digits[dist(rd)];
	}
	SetDlgItemText(IDC_NET_KEY, key);
	return 0;
}

//
// Serial port / pipe
//

void CComKernelPage::Load(KernelConnection const& conn) {
	CComboBox ports(GetDlgItem(IDC_COM_PORT));
	for (auto& port : GetSerialPorts())
		ports.AddString(port);
	SetComboText(ports, conn.ComPort);

	CComboBox bauds(GetDlgItem(IDC_COM_BAUD));
	for (auto baud : { L"9600", L"19200", L"38400", L"57600", L"115200" })
		bauds.AddString(baud);
	SetComboText(bauds, conn.ComBaud);

	SetDlgItemText(IDC_COM_PIPENAME, conn.PipeName);
	CEdit(GetDlgItem(IDC_COM_RESETS)).SetLimitText(4);
	SetDlgItemText(IDC_COM_RESETS, conn.Resets);
	CheckDlgButton(IDC_COM_RECONNECT, conn.Reconnect ? BST_CHECKED : BST_UNCHECKED);
	CheckRadioButton(IDC_COM_SERIAL, IDC_COM_PIPE, conn.ComPipe ? IDC_COM_PIPE : IDC_COM_SERIAL);
	OnChanged();
}

void CComKernelPage::OnChanged() {
	bool pipe = IsDlgButtonChecked(IDC_COM_PIPE) == BST_CHECKED;
	// (not the labels: dark mode draws a disabled one embossed, smeared)
	for (UINT id : { IDC_COM_PORT, IDC_COM_BAUD })
		GetDlgItem(id).EnableWindow(!pipe);
	for (UINT id : { IDC_COM_PIPENAME, IDC_COM_RESETS, IDC_COM_RECONNECT })
		GetDlgItem(id).EnableWindow(pipe);
}

CString CComKernelPage::Validate() {
	if (IsDlgButtonChecked(IDC_COM_PIPE) == BST_CHECKED) {
		auto pipe = GetText(IDC_COM_PIPENAME);
		if (pipe.IsEmpty() || pipe.Left(2) != L"\\\\" || pipe.FindOneOf(L",=") >= 0)
			return L"The pipe is a named pipe's path, e.g. \\\\.\\pipe\\com_1 (the virtual machine's serial port is connected to it).";
		auto resets = GetText(IDC_COM_RESETS);
		if (!resets.IsEmpty() && !IsNumber(resets, 0, 9999))
			return L"Resets is a number (0 for a virtual machine), or empty.";
	}
	else {
		if (!IsPlainValue(GetText(IDC_COM_PORT)))
			return L"Choose the serial port (e.g. COM1).";
		if (!IsNumber(GetText(IDC_COM_BAUD), 1, 10000000))
			return L"The baud rate is a number, e.g. 115200 (the same as the target's).";
	}
	return L"";
}

CString CComKernelPage::Format() {
	CString text;
	if (IsDlgButtonChecked(IDC_COM_PIPE) == BST_CHECKED) {
		text = L"com:pipe,port=" + GetText(IDC_COM_PIPENAME);
		if (auto resets = GetText(IDC_COM_RESETS); !resets.IsEmpty())
			text += L",resets=" + resets;
		if (IsDlgButtonChecked(IDC_COM_RECONNECT) == BST_CHECKED)
			text += L",reconnect";
	}
	else {
		text.Format(L"com:port=%s,baud=%s", (PCWSTR)GetText(IDC_COM_PORT), (PCWSTR)GetText(IDC_COM_BAUD));
	}
	return text;
}

CString CComKernelPage::Setup() {
	bool pipe = IsDlgButtonChecked(IDC_COM_PIPE) == BST_CHECKED;
	// a pipe is a virtual machine's serial port (COM1, as a rule); the baud rate of a pipe does not matter
	int port = 1;
	CString baud = L"115200";
	if (!pipe) {
		auto name = GetText(IDC_COM_PORT);
		if (name.Left(3).CompareNoCase(L"COM") == 0 && _wtoi(name.Mid(3)) > 0)
			port = _wtoi(name.Mid(3));
		if (auto b = GetText(IDC_COM_BAUD); !b.IsEmpty())
			baud = b;
	}
	CString text;
	text.Format(L"bcdedit /dbgsettings serial debugport:%d baudrate:%s && bcdedit /debug on", port, (PCWSTR)baud);
	return text;
}

//
// USB
//

void CUsbKernelPage::Load(KernelConnection const& conn) {
	SetDlgItemText(IDC_USB_TARGET, conn.UsbTarget);
}

CString CUsbKernelPage::Validate() {
	auto name = GetText(IDC_USB_TARGET);
	if (name.IsEmpty())
		return L"Enter the target name (the one bcdedit /dbgsettings usb set on the target).";
	if (!IsPlainValue(name))
		return L"The target name cannot have spaces, commas or '='.";
	return L"";
}

CString CUsbKernelPage::Format() {
	return L"usb:targetname=" + GetText(IDC_USB_TARGET);
}

CString CUsbKernelPage::Setup() {
	auto name = GetText(IDC_USB_TARGET);
	return L"bcdedit /dbgsettings usb targetname:" + (name.IsEmpty() ? CString(L"<name>") : name) + L" && bcdedit /debug on";
}

//
// Custom
//

void CCustomKernelPage::Load(KernelConnection const& conn) {
	SetText(conn.Custom);
}

void CCustomKernelPage::SetText(CString const& text) {
	SetDlgItemText(IDC_CUSTOM_CONN, text);
}

CString CCustomKernelPage::Validate() {
	auto text = GetText(IDC_CUSTOM_CONN);
	if (text.IsEmpty())
		return L"Enter a connection string.";
	if (text.Find(L':') <= 0)
		return L"A connection string starts with the transport and a colon (net:, com:, usb:, 1394:, ...).";
	return L"";
}

CString CCustomKernelPage::Format() {
	return GetText(IDC_CUSTOM_CONN);
}

//
// the dialog
//

template<typename T>
void CAttachKernelDlg::AddPage(PCWSTR title, KernelConnection const& conn) {
	auto page = std::make_unique<T>();
	page->Create(m_Tabs);
	page->Load(conn);
	// (light mode: the page's background as the tab's body)
	if (!WTLHelper::IsDarkMode())
		::EnableThemeDialogTexture(page->m_hWnd, ETDT_ENABLETAB);
	page->Changed = [this] { UpdateConnection(); };
	m_Tabs.InsertItem(m_Tabs.GetItemCount(), title);
	if constexpr (std::is_same_v<T, CCustomKernelPage>)
		m_CustomPage = page.get();
	m_Pages.push_back(std::move(page));
}

LRESULT CAttachKernelDlg::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
	AdjustOKCancelButtons(IDI_OK, IDI_DELETE);
	SetDialogIcon(IDI_ATTACH_KERNEL);

	m_Tabs.Attach(GetDlgItem(IDC_KERNEL_TABS));
	// the pages are the tab control's children: tabbing goes into them
	m_Tabs.ModifyStyleEx(0, WS_EX_CONTROLPARENT);

	auto conn = KernelConnection::Parse(m_Connection);
	// the order of KernelConnection::Kind
	AddPage<CNetKernelPage>(L"Network", conn);
	AddPage<CComKernelPage>(L"Serial / Pipe", conn);
	AddPage<CUsbKernelPage>(L"USB", conn);
	AddPage<CCustomKernelPage>(L"Connection String", conn);

	CRect rc;
	m_Tabs.GetClientRect(&rc);
	m_Tabs.AdjustRect(FALSE, &rc);
	for (auto& page : m_Pages)
		::SetWindowPos(page->Window(), nullptr, rc.left, rc.top, rc.Width(), rc.Height(), SWP_NOZORDER | SWP_NOACTIVATE);

	m_Ready = true;
	int index = static_cast<int>(conn.Type);
	m_Tabs.SetCurSel(index);
	ShowPage(index);
	CenterWindow(GetParent());

	// the first field of the page
	if (auto first = ::GetNextDlgTabItem(m_Pages[index]->Window(), nullptr, FALSE))
		GotoDlgCtrl(first);
	return FALSE;
}

LRESULT CAttachKernelDlg::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	m_Ready = false;
	for (auto& page : m_Pages)
		page->Destroy();
	bHandled = FALSE;
	return 0;
}

void CAttachKernelDlg::ShowPage(int index) {
	if (index < 0 || index >= (int)m_Pages.size())
		return;

	// to the connection string: what the page it comes from makes, as a start for changing it
	if (m_Pages[index].get() == m_CustomPage && m_Current >= 0 && m_Current != index) {
		auto& prev = m_Pages[m_Current];
		if (prev->Validate().IsEmpty())
			m_CustomPage->SetText(prev->Format());
	}

	m_Current = index;
	for (int i = 0; i < (int)m_Pages.size(); i++)
		::ShowWindow(m_Pages[i]->Window(), i == index ? SW_SHOW : SW_HIDE);
	UpdateConnection();
}

void CAttachKernelDlg::UpdateConnection() {
	if (!m_Ready || m_Current < 0)
		return;

	auto& page = m_Pages[m_Current];
	auto error = page->Validate();
	SetText(m_hWnd, IDC_KERNELCONN, error.IsEmpty() ? page->Format() : CString());
	SetText(m_hWnd, IDC_KERNEL_STATUS, error);
	SetText(page->Window(), IDC_KERNEL_SETUP, page->Setup());
	GetDlgItem(IDOK).EnableWindow(error.IsEmpty());
}

LRESULT CAttachKernelDlg::OnTabChanged(int, LPNMHDR, BOOL&) {
	ShowPage(m_Tabs.GetCurSel());
	return 0;
}

LRESULT CAttachKernelDlg::OnCloseCmd(WORD, WORD wID, HWND, BOOL&) {
	if (wID == IDOK) {
		auto& page = m_Pages[m_Current];
		if (!page->Validate().IsEmpty())
			return 0;
		m_Connection = page->Format();
	}
	EndDialog(wID);
	return 0;
}
