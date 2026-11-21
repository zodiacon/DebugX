#include "pch.h"
#include "SourceDocuments.h"
#include "SourceView.h"
#include "resource.h"

using namespace DebugEngine;
using namespace WTLDock;

namespace {
	constexpr std::wstring_view IdPrefix = L"source:";

	std::wstring FileName(const std::wstring& path) {
		auto slash = path.find_last_of(L"\\/");
		return slash == std::wstring::npos ? path : path.substr(slash + 1);
	}
}

void SourceDocuments::Init(CDockHost& dock, HWND statusBar) {
	m_Dock = &dock;
	m_StatusBar = statusBar;
	m_Icon = AtlLoadIconImage(IDI_OPEN, LR_DEFAULTCOLOR | LR_SHARED, 16, 16);
	DebugManager::Session().AddObserver(this);
}

void SourceDocuments::Term() {
	DebugManager::Session().RemoveObserver(this);
}

bool SourceDocuments::IsSourceId(std::wstring_view id) {
	return id.starts_with(IdPrefix);
}

HWND SourceDocuments::CreateView(const DockPane& pane, HWND parent) {
	auto view = new CSourceView(pane.Id().substr(IdPrefix.size()));
	auto hWnd = view->Create(parent);
	if (!hWnd)
		delete view;
	return hWnd;
}

DockPane* SourceDocuments::MakePane(DockLayout& layout, const std::wstring& id) {
	if (!IsSourceId(id))
		return nullptr;
	// (a file that is gone since is not brought back)
	auto path = id.substr(IdPrefix.size());
	auto attributes = ::GetFileAttributesW(path.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
		return nullptr;

	PaneDesc desc;
	desc.Id = id;
	desc.Title = FileName(path);
	desc.Tooltip = path;
	desc.Icon = m_Icon;
	desc.Kind = PaneKind::Document;
	return layout.AddPane(desc);
}

void SourceDocuments::OnPaneClosed(DockPane* pane) {
	if (!pane || !IsSourceId(pane->Id()))
		return;
	if (pane->hWnd)
		::DestroyWindow(pane->hWnd);
	m_Dock->Layout().RemovePane(pane);
}

DockPane* SourceDocuments::FindPane(const std::wstring& path) const {
	for (auto& pane : m_Dock->Layout().Panes()) {
		auto& id = pane->Id();
		if (IsSourceId(id) && _wcsicmp(id.c_str() + IdPrefix.size(), path.c_str()) == 0)
			return pane.get();
	}
	return nullptr;
}

CSourceView* SourceDocuments::FindView(const std::wstring& path) const {
	for (auto view : CSourceView::Views()) {
		if (_wcsicmp(view->Path().c_str(), path.c_str()) == 0)
			return view;
	}
	return nullptr;
}

CSourceView* SourceDocuments::Open(const std::wstring& path, ULONG line, bool activate) {
	auto& layout = m_Dock->Layout();
	auto pane = FindPane(path);
	if (!pane) {
		pane = MakePane(layout, std::wstring(IdPrefix) + path);
		if (!pane)
			return nullptr;
	}

	if (pane->State() == PaneState::Hidden) {
		// with the other source files; the first one goes above the Command view, which is not to be covered up
		DockPane* with = nullptr;
		for (auto& other : layout.Panes()) {
			if (other.get() != pane && IsSourceId(other->Id()) && other->State() == PaneState::Document && other->Group()) {
				with = other.get();
				break;
			}
		}
		bool placed = with && layout.DockTo(pane, with->Group(), DockPosition::Tab);
		if (!placed) {
			auto command = layout.FindPane(L"command");
			placed = command && command->State() == PaneState::Document && command->Group() &&
				layout.DockTo(pane, command->Group(), DockPosition::Top);
		}
		if (!placed)
			layout.Show(pane);
	}

	if (activate)
		m_Dock->ActivatePane(pane);
	else
		layout.Activate(pane);		// the tab in front, the focus where it was

	auto view = FindView(path);
	if (view && line)
		view->GoToLine(line);
	return view;
}

void SourceDocuments::SetStatus(const std::wstring& text) {
	if (m_StatusBar)
		::SetWindowText(m_StatusBar, text.empty() ? L"Ready" : text.c_str());
}

void SourceDocuments::OnResumed() {
	// (no arrows while it runs: they would be out of date)
	for (auto view : CSourceView::Views())
		view->SetLocation(0, 0, false);
}

void SourceDocuments::OnTargetEnded() {
	SetStatus({});
}

void SourceDocuments::RequestUpdate() {
	// (the events come in bunches: one update at a time, and one more if anything came while it ran)
	if (m_Updating) {
		m_UpdatePending = true;
		return;
	}
	Update();
}

Task<void> SourceDocuments::Update() {
	auto alive = m_Lifetime.Watch();
	m_Updating = true;
	auto& session = DebugManager::Session();
	auto& dbg = DebugManager::Debugger();

	do {
		m_UpdatePending = false;
		if (!session.HasTarget() || !session.IsBroken())
			break;

		ULONG64 ip = 0, frameIp = 0;
		ULONG frame = 0;
		auto hr = co_await dbg.GetInstructionOffset(ip);
		if (!*alive)
			co_return;
		if (SUCCEEDED(hr) && FAILED(co_await dbg.GetScope(frameIp, frame)))
			frame = 0;
		if (!*alive)
			co_return;
		if (FAILED(hr))
			break;

		// a frame above the innermost is where its call returns to: the call is the line before that address
		std::vector<ULONG64> offsets{ ip };
		if (frame > 0)
			offsets.push_back(frameIp - 1);
		std::vector<SourceLocation> locations;
		co_await dbg.GetSourceLocations(offsets, locations);
		if (!*alive)
			co_return;
		if (locations.size() != offsets.size())
			break;

		auto& current = locations[0];
		auto& scope = locations.back();
		auto& shown = frame > 0 ? scope : current;
		CSourceView* scopeView = nullptr;
		if (!shown.Path.empty()) {
			scopeView = Open(shown.Path, 0, false);
			if (scopeView)
				scopeView->SetSymbolFile(shown.File);
			SetStatus({});
		}
		else if (!shown.File.empty())
			SetStatus(L"Source file not found: " + shown.File + L" (Options > Symbol and Source Paths)");
		else
			SetStatus(L"No source line information here (see the Disassembly window)");

		for (auto view : CSourceView::Views()) {
			auto onLine = [&](const SourceLocation& location) -> ULONG {
				return !location.Path.empty() && _wcsicmp(location.Path.c_str(), view->Path().c_str()) == 0 ? location.Line : 0;
			};
			view->SetLocation(onLine(current), frame > 0 ? onLine(scope) : 0, view == scopeView);
		}
	} while (m_UpdatePending);
	m_Updating = false;
}
