#include "pch.h"
#include "AppFonts.h"
#include "AppSettings.h"
#include "ScintillaCtrl.h"
#include <format>

using namespace AppFonts;

namespace {
	struct Entry {
		FontSpec Spec;
		CFont Font;
	};

	Entry g_Code{ { L"Consolas", 90 } }, g_Ui{ { L"Segoe UI", 90 } };

	Entry& Get(Kind kind) {
		return kind == Kind::Code ? g_Code : g_Ui;
	}

	int ScreenDpi() {
		// (as CreatePointFont: the screen's)
		CClientDC dc(nullptr);
		return dc.GetDeviceCaps(LOGPIXELSY);
	}

	LOGFONT ToLogFont(const FontSpec& spec) {
		LOGFONT lf{};
		lf.lfHeight = -::MulDiv(spec.PointSize10, ScreenDpi(), 720);
		lf.lfWeight = spec.Weight;
		lf.lfItalic = spec.Italic ? TRUE : FALSE;
		lf.lfCharSet = DEFAULT_CHARSET;
		lf.lfQuality = CLEARTYPE_QUALITY;
		::StringCchCopy(lf.lfFaceName, _countof(lf.lfFaceName), spec.Face.c_str());
		return lf;
	}

	HFONT Create(const FontSpec& spec) {
		auto lf = ToLogFont(spec);
		return ::CreateFontIndirect(&lf);
	}

	// "face;size;weight;italic" (size in tenths of a point); empty or malformed: the default stays
	bool Parse(const std::wstring& s, FontSpec& spec) {
		FontSpec result;
		size_t start = 0;
		std::vector<std::wstring> parts;
		for (;;) {
			auto end = s.find(L';', start);
			parts.push_back(s.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start));
			if (end == std::wstring::npos)
				break;
			start = end + 1;
		}
		if (parts.size() != 4 || parts[0].empty())
			return false;
		result.Face = parts[0];
		result.PointSize10 = _wtoi(parts[1].c_str());
		result.Weight = _wtoi(parts[2].c_str());
		result.Italic = _wtoi(parts[3].c_str()) != 0;
		if (result.PointSize10 < 40 || result.PointSize10 > 720 || result.Weight <= 0 || result.Weight > 1000)
			return false;
		spec = std::move(result);
		return true;
	}

	std::wstring Format(const FontSpec& spec) {
		return std::format(L"{};{};{};{}", spec.Face, spec.PointSize10, spec.Weight, spec.Italic ? 1 : 0);
	}

	void Broadcast(HWND frame, Kind kind) {
		auto wParam = static_cast<WPARAM>(kind);
		CWindow(frame).SendMessageToDescendants(FontsChangedMessage, wParam);
		// the windows the frame owns (floating panes) are not its children
		struct Data {
			HWND Frame;
			WPARAM WParam;
		} data{ frame, wParam };
		::EnumThreadWindows(::GetCurrentThreadId(), [](HWND hWnd, LPARAM lParam) -> BOOL {
			auto& d = *reinterpret_cast<Data*>(lParam);
			if (hWnd != d.Frame && ::GetAncestor(hWnd, GA_ROOTOWNER) == d.Frame)
				CWindow(hWnd).SendMessageToDescendants(FontsChangedMessage, d.WParam);
			return TRUE;
			}, reinterpret_cast<LPARAM>(&data));
	}
}

void AppFonts::Load() {
	auto& settings = AppSettings::Get();
	Parse(settings.CodeFont(), g_Code.Spec);
	Parse(settings.UiFont(), g_Ui.Spec);
	for (auto e : { &g_Code, &g_Ui }) {
		if (!e->Font.IsNull())
			e->Font.DeleteObject();
		e->Font.Attach(Create(e->Spec));
	}
}

const FontSpec& AppFonts::Spec(Kind kind) {
	return Get(kind).Spec;
}

HFONT AppFonts::Font(Kind kind) {
	auto& e = Get(kind);
	if (e.Font.IsNull())
		e.Font.Attach(Create(e.Spec));
	return e.Font;
}

bool AppFonts::Choose(Kind kind, HWND frame) {
	auto& e = Get(kind);
	auto lf = ToLogFont(e.Spec);
	CHOOSEFONT cf{ sizeof(cf) };
	cf.hwndOwner = frame;
	cf.lpLogFont = &lf;
	cf.Flags = CF_INITTOLOGFONTSTRUCT | CF_SCREENFONTS | CF_NOVERTFONTS | CF_NOSCRIPTSEL | CF_FORCEFONTEXIST
		| (kind == Kind::Code ? CF_FIXEDPITCHONLY : 0);
	if (!::ChooseFont(&cf))
		return false;

	FontSpec spec;
	spec.Face = lf.lfFaceName;
	spec.PointSize10 = cf.iPointSize;
	spec.Weight = lf.lfWeight ? lf.lfWeight : FW_NORMAL;
	spec.Italic = lf.lfItalic != 0;
	if (spec == e.Spec)
		return false;

	// the windows go on using the old font until they have the new one
	CFont old(e.Font.Detach());
	e.Spec = std::move(spec);
	e.Font.Attach(Create(e.Spec));
	auto text = Format(e.Spec);
	if (kind == Kind::Code)
		AppSettings::Get().CodeFont(text.c_str());
	else
		AppSettings::Get().UiFont(text.c_str());
	Broadcast(frame, kind);
	return true;
}

void AppFonts::ApplyCodeFont(Scintilla::ScintillaCall& sci) {
	auto& spec = g_Code.Spec;
	constexpr int style = static_cast<int>(Scintilla::StylesCommon::Default);
	sci.StyleSetFont(style, ToUtf8(spec.Face).c_str());
	sci.StyleSetSizeFractional(style, spec.PointSize10 * 10);
	sci.StyleSetWeight(style, static_cast<Scintilla::FontWeight>(spec.Weight));
	sci.StyleSetItalic(style, spec.Italic);
}

int AppFonts::TextHeight(HFONT font) {
	CClientDC dc(nullptr);
	auto old = dc.SelectFont(font);
	TEXTMETRIC tm{};
	dc.GetTextMetrics(&tm);
	dc.SelectFont(old);
	return tm.tmHeight;
}

int AppFonts::TextWidth(HFONT font, PCWSTR text) {
	CClientDC dc(nullptr);
	auto old = dc.SelectFont(font);
	SIZE size{};
	dc.GetTextExtent(text, -1, &size);
	dc.SelectFont(old);
	return size.cx;
}
