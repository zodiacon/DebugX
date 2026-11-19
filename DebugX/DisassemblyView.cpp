#include "stdafx.h"
#include "DisassemblyView.h"
#include "DebugManager.h"
#include "InputDlg.h"
#include <format>
#include <scintilla/ILexer.h>
#include <lexilla/Lexilla.h>
#include <lexilla/SciLexer.h>

using namespace DebugEngine;
using namespace Scintilla;

LRESULT CDisassemblyView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_Text.Create(*this, nullptr, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
		WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, TextId);
	m_Text.InitCall();

	m_Text->SetReadOnly(true);
	m_Text->SetUndoCollection(false);
	m_Text->SetWrapMode(Wrap::None);
	m_Text->StyleSetFont(static_cast<int>(StylesCommon::Default), "Consolas");
	m_Text->StyleSetSizeFractional(static_cast<int>(StylesCommon::Default), 900);

	if (auto lexer = ::CreateLexer("asm")) {
		m_Text->SetILexer(lexer);
		// the "asm" lexer only recognizes a word as an instruction/register/etc. if it is in one of
		// these keyword lists (list numbers per LexAsm.cxx / asmWordListDesc) -- it ships with none
		// of its own, unlike a lexer for a fixed language, so a mnemonic is plain SCE_ASM_IDENTIFIER
		// (the default style) without this
		m_Text->SetKeyWords(0,		// CPU instructions
			"mov movzx movsx movsxd lea push pop pushf pushfq popf popfq xchg cmpxchg cmpxchg8b cmpxchg16b "
			"xadd cwde cdq cdqe cqo cbw cwd bswap movsb movsw movsd movsq lodsb lodsw lodsd lodsq stosb stosw "
			"stosd stosq scasb scasw scasd scasq cmpsb cmpsw cmpsd cmpsq ins insb insw insd outs outsb outsw outsd "
			"add adc sub sbb mul imul div idiv inc dec neg cmp adcx adox "
			"and or xor not test shl shr sal sar rol ror rcl rcr shld shrd bt bts btr btc bsf bsr popcnt "
			"lzcnt tzcnt andn blsi blsr blsmsk "
			"jmp je jz jne jnz jg jnle jge jnl jl jnge jle jng ja jnbe jae jnb jb jnae jbe jna jc jnc jo jno "
			"js jns jp jpe jnp jpo jcxz jecxz jrcxz call ret retn retf int int3 into iret iretd iretq "
			"loop loope loopne loopz loopnz enter leave syscall sysenter sysexit sysret ud2 hlt nop pause "
			"endbr64 endbr32 lock rep repe repz repne repnz "
			"clc stc cmc cld std cli sti lahf sahf "
			"seta setae setb setbe setc sete setg setge setl setle setna setnae setnb setnbe setnc setne "
			"setng setnge setnl setnle setno setnp setns setnz seto setp setpe setpo sets setz "
			"cmova cmovae cmovb cmovbe cmovc cmove cmovg cmovge cmovl cmovle cmovna cmovnae cmovnb cmovnbe "
			"cmovnc cmovne cmovng cmovnge cmovnl cmovnle cmovno cmovnp cmovns cmovnz cmovo cmovp cmovpe cmovpo cmovs cmovz "
			"cpuid rdtsc rdtscp xgetbv xsetbv rdrand rdseed clflush clflushopt lfence sfence mfence "
			"prefetchnta prefetcht0 prefetcht1 prefetcht2 wbinvd invd clts lgdt sgdt lidt sidt lldt sldt "
			"ltr str arpl lar lsl verr verw invlpg wrmsr rdmsr rdpmc in out aad aam aaa aas daa das xlat xlatb bound "
			"movaps movups movapd movupd movss movdqa movdqu movq movd addps addss addpd addsd subps subss "
			"subpd subsd mulps mulss mulpd mulsd divps divss divpd divsd xorps xorpd andps andpd andnps andnpd "
			"orps orpd cmpps cmpss cmppd cmpsd ucomiss ucomisd comiss comisd cvtsi2ss cvtsi2sd cvtss2si cvtsd2si "
			"cvttss2si cvttsd2si cvtss2sd cvtsd2ss cvtdq2ps cvtps2dq cvttps2dq cvtps2pd cvtpd2ps "
			"punpcklbw punpcklwd punpckldq punpcklqdq punpckhbw punpckhwd punpckhdq punpckhqdq "
			"packsswb packssdw packuswb paddb paddw paddd paddq psubb psubw psubd psubq pand pandn por pxor "
			"pcmpeqb pcmpeqw pcmpeqd pcmpgtb pcmpgtw pcmpgtd pshufd pshufb pshuflw pshufhw pslldq psrldq "
			"pslld psrld psllq psrlq psllw psrlw psrad psraw pinsrb pinsrw pinsrd pinsrq pextrb pextrw pextrd "
			"pextrq pmovmskb movmskps movmskpd shufps shufpd unpcklps unpckhps unpcklpd unpckhpd sqrtps sqrtss "
			"sqrtpd sqrtsd minps minss minpd minsd maxps maxss maxpd maxsd rcpps rsqrtps ldmxcsr stmxcsr "
			"fxsave fxrstor xsave xsaves xrstor xrstors vzeroupper vzeroall "
			"vmovaps vmovups vmovdqa vmovdqu vmovss vmovsd vaddps vaddss vsubps vmulps vxorps vpxor vpand vpor "
			"vpshufd vpcmpeqb vpcmpeqd vinsertf128 vextractf128 vbroadcastss vpbroadcastd vperm2f128 vpermq vpunpcklqdq");
		m_Text->SetKeyWords(1,		// FPU instructions
			"fld fst fstp fild fist fistp fadd fsub fsubr fmul fdiv fdivr fabs fchs fcom fcomp fcompp fucom "
			"fucomp fucompp fcomi fcomip fucomi fucomip fxch fnop ffree fld1 fldz fldpi fldl2e fldl2t fldlg2 "
			"fldln2 fscale fsqrt fsin fcos fsincos fptan fpatan f2xm1 fyl2x fyl2xp1 fnstsw fstsw fnstcw fstcw "
			"fldcw fnclex fclex fninit finit fwait wait frndint fxtract fprem fprem1 fdecstp fincstp "
			"fsave fnsave frstor fxam ftst");
		m_Text->SetKeyWords(2,		// Registers
			"rax rbx rcx rdx rsi rdi rbp rsp r8 r9 r10 r11 r12 r13 r14 r15 rip "
			"eax ebx ecx edx esi edi ebp esp r8d r9d r10d r11d r12d r13d r14d r15d eip "
			"ax bx cx dx si di bp sp r8w r9w r10w r11w r12w r13w r14w r15w "
			"al bl cl dl ah bh ch dh sil dil bpl spl r8b r9b r10b r11b r12b r13b r14b r15b "
			"cs ds es fs gs ss eflags rflags "
			"cr0 cr1 cr2 cr3 cr4 cr8 dr0 dr1 dr2 dr3 dr6 dr7 "
			"xmm0 xmm1 xmm2 xmm3 xmm4 xmm5 xmm6 xmm7 xmm8 xmm9 xmm10 xmm11 xmm12 xmm13 xmm14 xmm15 "
			"ymm0 ymm1 ymm2 ymm3 ymm4 ymm5 ymm6 ymm7 ymm8 ymm9 ymm10 ymm11 ymm12 ymm13 ymm14 ymm15 "
			"mm0 mm1 mm2 mm3 mm4 mm5 mm6 mm7 st0 st1 st2 st3 st4 st5 st6 st7");
	}

	m_Text->SetMarginTypeN(MarginSymbols, MarginType::Symbol);
	m_Text->SetMarginWidthN(MarginSymbols, 18);
	m_Text->SetMarginMaskN(MarginSymbols, (1 << MarkerBreakpoint) | (1 << MarkerCurrent) | (1 << MarkerFrame));
	m_Text->SetMarginSensitiveN(MarginSymbols, true);
	m_Text->MarkerDefine(MarkerBreakpoint, MarkerSymbol::Circle);
	m_Text->MarkerDefine(MarkerCurrent, MarkerSymbol::ShortArrow);
	m_Text->MarkerSetFore(MarkerCurrent, static_cast<Colour>(RGB(0, 0, 0)));
	m_Text->MarkerSetBack(MarkerCurrent, static_cast<Colour>(RGB(255, 230, 0)));
	// where the scope frame (chosen in the call stack) is: the instruction its call returns to
	m_Text->MarkerDefine(MarkerFrame, MarkerSymbol::ShortArrow);
	m_Text->MarkerSetFore(MarkerFrame, static_cast<Colour>(RGB(0, 0, 0)));
	m_Text->MarkerSetBack(MarkerFrame, static_cast<Colour>(RGB(80, 200, 80)));

	ApplyColors();
	InitPane();
	return 0;
}

LRESULT CDisassemblyView::OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
	ExitPane();
	handled = FALSE;
	return 0;
}

LRESULT CDisassemblyView::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto cx = GET_X_LPARAM(lParam), cy = GET_Y_LPARAM(lParam);
	if (cx && cy)
		m_Text.MoveWindow(0, 0, cx, cy);
	return 0;
}

LRESULT CDisassemblyView::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
	m_Text.SetFocus();
	return 0;
}

LRESULT CDisassemblyView::OnThemeChanged(UINT, WPARAM, LPARAM, BOOL&) {
	ApplyColors();
	::SetWindowTheme(m_Text, WTLHelper::IsDarkMode() ? L"DarkMode_Explorer" : L"Explorer", nullptr);
	m_Text.Invalidate();
	return 0;
}

void CDisassemblyView::ApplyColors() {
	auto fore = static_cast<Colour>(::GetSysColor(COLOR_WINDOWTEXT));
	auto back = static_cast<Colour>(::GetSysColor(COLOR_WINDOW));
	m_Text->StyleSetFore(static_cast<int>(StylesCommon::Default), fore);
	m_Text->StyleSetBack(static_cast<int>(StylesCommon::Default), back);
	m_Text->StyleClearAll();		// also wipes the "asm" lexer's own styles below, back to plain text
	m_Text->SetCaretFore(fore);
	m_Text->SetSelBack(true, static_cast<Colour>(::GetSysColor(COLOR_HIGHLIGHT)));
	m_Text->MarkerSetFore(MarkerBreakpoint, static_cast<Colour>(RGB(255, 255, 255)));
	m_Text->MarkerSetBack(MarkerBreakpoint, static_cast<Colour>(WTLHelper::IsDarkMode() ? RGB(200, 60, 60) : RGB(220, 0, 0)));

	// the "asm" lexer's token styles (SciLexer.h); StyleClearAll left them all as plain text, above
	const bool dark = WTLHelper::IsDarkMode();
	auto setFore = [&](int style, COLORREF light, COLORREF inDark) {
		m_Text->StyleSetFore(style, static_cast<Colour>(dark ? inDark : light));
	};
	setFore(SCE_ASM_COMMENT, RGB(0, 128, 0), RGB(96, 180, 96));
	setFore(SCE_ASM_COMMENTBLOCK, RGB(0, 128, 0), RGB(96, 180, 96));
	setFore(SCE_ASM_COMMENTDIRECTIVE, RGB(0, 128, 0), RGB(96, 180, 96));
	setFore(SCE_ASM_NUMBER, RGB(9, 134, 88), RGB(181, 206, 168));
	setFore(SCE_ASM_STRING, RGB(163, 21, 21), RGB(214, 157, 133));
	setFore(SCE_ASM_CHARACTER, RGB(163, 21, 21), RGB(214, 157, 133));
	setFore(SCE_ASM_STRINGEOL, RGB(255, 0, 0), RGB(255, 120, 120));
	setFore(SCE_ASM_REGISTER, RGB(0, 128, 128), RGB(78, 201, 176));
	setFore(SCE_ASM_DIRECTIVE, RGB(128, 0, 128), RGB(197, 134, 192));
	setFore(SCE_ASM_DIRECTIVEOPERAND, RGB(128, 0, 128), RGB(197, 134, 192));
	setFore(SCE_ASM_CPUINSTRUCTION, RGB(0, 0, 255), RGB(86, 156, 214));
	setFore(SCE_ASM_MATHINSTRUCTION, RGB(0, 0, 255), RGB(86, 156, 214));
	setFore(SCE_ASM_EXTINSTRUCTION, RGB(0, 0, 255), RGB(86, 156, 214));
	m_Text->StyleSetBold(SCE_ASM_CPUINSTRUCTION, true);
	m_Text->StyleSetBold(SCE_ASM_MATHINSTRUCTION, true);
	m_Text->StyleSetBold(SCE_ASM_EXTINSTRUCTION, true);
}

void CDisassemblyView::OnTargetEnded() {
	m_SourceFiles.clear();
	m_LineAddresses.clear();
	m_Breakpoints.clear();
	m_Text->SetReadOnly(false);
	m_Text->ClearAll();
	m_Text->SetReadOnly(true);
	MarkDirty();
}

Task<void> CDisassemblyView::Refresh() {
	auto alive = m_Lifetime.Watch();

	ULONG64 ip = 0;
	co_await DebugManager::DebuggerX().GetInstructionOffset(ip);
	if (!*alive)
		co_return;

	// in another frame than 0, the window starts where that frame is, not where the thread is
	ULONG64 frameIp = 0;
	ULONG frame = 0;
	if (FAILED(co_await DebugManager::DebuggerX().GetScope(frameIp, frame)))
		frame = 0;
	if (!*alive)
		co_return;
	if (frame == 0)
		frameIp = 0;

	std::vector<DisassemblyLine> lines;
	auto goTo = m_GoToAddress;
	co_await DebugManager::DebuggerX().Disassemble(goTo ? goTo : frameIp ? frameIp : ip, LineCount, lines);
	if (!*alive)
		co_return;

	std::vector<BreakpointInfo> breakpoints;
	co_await DebugManager::DebuggerX().EnumBreakpoints(breakpoints);
	if (!*alive)
		co_return;

	// where each instruction comes from, for the source lines between them
	std::vector<SourceLocation> locations;
	if (m_ShowSource) {
		std::vector<ULONG64> offsets;
		offsets.reserve(lines.size());
		for (auto& line : lines)
			offsets.push_back(line.Address);
		co_await DebugManager::DebuggerX().GetSourceLocations(std::move(offsets), locations);
		if (!*alive)
			co_return;
		if (locations.size() != lines.size())
			locations.clear();
	}

	std::wstring text;
	m_LineAddresses.clear();
	m_LineAddresses.reserve(lines.size());
	std::wstring lastFile;
	ULONG lastLine = 0;
	for (size_t i = 0; i < lines.size(); i++) {
		auto& line = lines[i];
		if (!locations.empty() && !locations[i].File.empty() && (locations[i].Line != lastLine || locations[i].File != lastFile)) {
			// (a line of its own, with no address: the commands do nothing on it)
			auto& loc = locations[i];
			lastFile = loc.File;
			lastLine = loc.Line;
			auto name = loc.File.substr(loc.File.find_last_of(L"\\/") + 1);
			std::wstring source;
			if (!loc.Path.empty()) {
				auto& fileLines = SourceLines(loc.Path);
				if (loc.Line >= 1 && loc.Line <= fileLines.size())
					source = fileLines[loc.Line - 1];
			}
			text += std::format(L"; {}({}): {}\n", name, loc.Line, source);
			m_LineAddresses.push_back(0);
		}
		text += line.Text;
		text += L'\n';
		m_LineAddresses.push_back(line.Address);
	}
	m_Breakpoints = std::move(breakpoints);

	// (the same place again -- a breakpoint toggled there -- stays scrolled where it was)
	auto samePlace = goTo && goTo == m_ShownGoTo;
	auto firstVisible = m_Text->FirstVisibleLine();
	auto caret = m_Text->CurrentPos();
	m_ShownGoTo = goTo;

	auto utf8 = ToUtf8(text);
	m_Text->SetReadOnly(false);
	m_Text->SetText(utf8.c_str());
	m_Text->SetReadOnly(true);

	m_Text->MarkerDeleteAll(MarkerBreakpoint);
	m_Text->MarkerDeleteAll(MarkerCurrent);
	m_Text->MarkerDeleteAll(MarkerFrame);
	Line currentLine = -1;
	for (size_t i = 0; i < m_LineAddresses.size(); i++) {
		auto address = m_LineAddresses[i];
		if (address == ip) {
			m_Text->MarkerAdd(static_cast<Line>(i), MarkerCurrent);
			if (!frameIp)
				currentLine = static_cast<Line>(i);
		}
		if (frameIp && address == frameIp) {
			m_Text->MarkerAdd(static_cast<Line>(i), MarkerFrame);
			currentLine = static_cast<Line>(i);
		}
		for (auto& bp : m_Breakpoints) {
			if (!bp.Deferred && bp.Offset == address) {
				m_Text->MarkerAdd(static_cast<Line>(i), MarkerBreakpoint);
				break;
			}
		}
	}
	if (samePlace) {
		m_Text->GotoPos(caret);
		m_Text->SetFirstVisibleLine(firstVisible);
	}
	else if (goTo)
		m_Text->GotoLine(0);
	else if (currentLine >= 0) {
		m_Text->GotoLine(currentLine);
		m_Text->SetFirstVisibleLine(currentLine > 10 ? currentLine - 10 : 0);
	}

	RefreshDone();
}

ULONG64 CDisassemblyView::AddressAtPosition(Position pos) {
	auto line = m_Text->LineFromPosition(pos);
	return line >= 0 && static_cast<size_t>(line) < m_LineAddresses.size() ? m_LineAddresses[line] : 0;
}

LRESULT CDisassemblyView::OnMarginClick(int, LPNMHDR hdr, BOOL&) {
	auto data = reinterpret_cast<const NotificationData*>(hdr);
	if (data->margin == MarginSymbols) {
		if (auto address = AddressAtPosition(data->position))
			ToggleBreakpointAt(address);
	}
	return 0;
}

Task<void> CDisassemblyView::ToggleBreakpointAt(ULONG64 address) {
	auto alive = m_Lifetime.Watch();

	for (auto& bp : m_Breakpoints) {
		if (!bp.Deferred && bp.Offset == address) {
			co_await DebugManager::DebuggerX().RemoveBreakpoint(bp.Id);
			co_return;
		}
	}
	co_await DebugManager::DebuggerX().AddBreakpoint(std::format(L"0x{:X}", address).c_str());
	// (nothing of this window's is touched after either await, so no need to check 'alive' before returning)
}

Task<void> CDisassemblyView::RunToCursor(ULONG64 address) {
	if (!DebugManager::Session().CanRun()) {
		::MessageBeep(MB_ICONWARNING);
		co_return;
	}
	co_await DebugManager::DebuggerX().ExecuteCommand(std::format(L"g 0x{:X}", address).c_str());
}

LRESULT CDisassemblyView::OnTextContextMenu(UINT, WPARAM, LPARAM lParam, BOOL&) {
	CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	CPoint client;
	if (pt.x != -1) {
		client = pt;
		m_Text.ScreenToClient(&client);
	}
	else {
		// invoked from the keyboard: the caret's own position
		auto caret = m_Text->CurrentPos();
		client = { m_Text->PointXFromPosition(caret), m_Text->PointYFromPosition(caret) };
		pt = client;
		m_Text.ClientToScreen(&pt);
	}
	auto address = AddressAtPosition(m_Text->PositionFromPoint(client.x, client.y));

	CMenu menu;
	menu.CreatePopupMenu();
	auto& session = DebugManager::Session();
	auto canRun = address && session.CanRun();
	menu.AppendMenu(address ? MF_STRING : (MF_STRING | MF_GRAYED), ID_DISASM_TOGGLEBREAKPOINT, L"Toggle &Breakpoint\tF9");
	menu.AppendMenu(canRun ? MF_STRING : (MF_STRING | MF_GRAYED), ID_DEBUG_RUNTOCURSOR, L"&Run to Cursor\tCtrl+F10");
	menu.AppendMenu(canRun ? MF_STRING : (MF_STRING | MF_GRAYED), ID_DEBUG_SETNEXTSTATEMENT, L"Set &Next Statement\tCtrl+Shift+F10");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(session.HasTarget() && !session.IsRunning() ? MF_STRING : (MF_STRING | MF_GRAYED), ID_EDIT_GOTO, L"&Go to Address...\tCtrl+G");
	enum { CmdShowSource = 1 };
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING | (m_ShowSource ? MF_CHECKED : 0), CmdShowSource, L"Show &Source Lines");
	auto id = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd);
	if (id == CmdShowSource) {
		m_ShowSource = !m_ShowSource;
		m_ShownGoTo = 0;
		RequestRefresh();
	}
	else if (id == ID_EDIT_GOTO)
		GoToAddress();
	else
		RunCommand(id, address);
	return 0;
}

LRESULT CDisassemblyView::OnGoTo(WORD, WORD, HWND, BOOL&) {
	GoToAddress();
	return 0;
}

Task<void> CDisassemblyView::GoToAddress() {
	auto alive = m_Lifetime.Watch();
	auto& session = DebugManager::Session();
	if (!session.HasTarget() || session.IsRunning()) {
		::MessageBeep(MB_ICONWARNING);
		co_return;
	}
	CInputDlg dlg(L"Go to Address", L"Address or expression (0x7ff6`1234, kernel32!CreateFileW, @rsp, ...):", m_GoToExpression.c_str());
	if (dlg.DoModal(m_hWnd) != IDOK)
		co_return;

	std::wstring expression((PCWSTR)dlg.GetText());
	ULONG64 address = 0;
	auto hr = co_await DebugManager::DebuggerX().Evaluate(expression.c_str(), address);
	if (!*alive)
		co_return;
	if (FAILED(hr) || address == 0) {
		AtlMessageBox(m_hWnd, std::format(L"\"{}\" is not an address (0x{:08X})", expression, static_cast<unsigned>(hr)).c_str(), IDS_TITLE, MB_ICONERROR);
		co_return;
	}
	m_GoToExpression = std::move(expression);
	m_GoToAddress = address;
	m_ShownGoTo = 0;
	RequestRefresh();
	m_Text.SetFocus();
}

LRESULT CDisassemblyView::OnCaretCommand(WORD, WORD id, HWND, BOOL&) {
	RunCommand(id, AddressAtPosition(m_Text->CurrentPos()));
	return 0;
}

void CDisassemblyView::RunCommand(UINT id, ULONG64 address) {
	if (!address)
		return;
	switch (id) {
		case ID_DISASM_TOGGLEBREAKPOINT: ToggleBreakpointAt(address); break;
		case ID_DEBUG_RUNTOCURSOR: RunToCursor(address); break;
		case ID_DEBUG_SETNEXTSTATEMENT: DebugManager::SetNextStatement(m_hWnd, address); break;
	}
}

const std::vector<std::wstring>& CDisassemblyView::SourceLines(const std::wstring& path) {
	auto it = m_SourceFiles.find(path);
	if (it != m_SourceFiles.end())
		return it->second;

	std::vector<std::wstring> lines;
	wil::unique_hfile file(::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, 0, nullptr));
	LARGE_INTEGER size{};
	if (file && ::GetFileSizeEx(file.get(), &size) && size.QuadPart < 64 << 20) {
		std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
		DWORD read = 0;
		if (bytes.empty() || ::ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr)) {
			bytes.resize(read);
			auto text = FromUtf8(ToUtf8Text(std::move(bytes)));
			size_t pos = 0;
			while (pos <= text.size()) {
				auto end = text.find(L'\n', pos);
				if (end == std::wstring::npos)
					end = text.size();
				auto line = text.substr(pos, end - pos);
				if (!line.empty() && line.back() == L'\r')
					line.pop_back();
				// (tabs as the code shows them would be wider than the room there is)
				std::replace(line.begin(), line.end(), L'\t', L' ');
				lines.push_back(std::move(line));
				pos = end + 1;
			}
		}
	}
	return m_SourceFiles.emplace(path, std::move(lines)).first->second;
}
