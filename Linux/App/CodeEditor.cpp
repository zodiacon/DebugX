#include "CodeEditor.h"
#include "GdbDebugger.h"
#include "MainWindow.h"
#include "SessionModel.h"
#include <QContextMenuEvent>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QToolTip>
#include <Qsci/qscilexermasm.h>
#include <Qsci/qscilexercpp.h>

using namespace DebugEngine;

bool IsDarkPalette() {
	return QPalette().color(QPalette::Base).lightness() < 128;
}

namespace {
	//
	// The lexers, with the keyword lists of the Windows views (Scintilla's asm lexer knows no instruction or register
	// of its own: a word is one only if it is in a list -- QScintilla numbers them from 1, Lexilla from 0; QsciLexerAsm is
	// abstract, its MASM flavor is Scintilla's "asm" lexer with ";" comments)
	//
	class CppLexer final : public QsciLexerCPP {
	public:
		using QsciLexerCPP::QsciLexerCPP;
		const char* keywords(int set) const override {
			switch (set) {
				case 1:
					return "alignas alignof and and_eq asm auto bitand bitor bool break case catch char char8_t char16_t char32_t class "
						"compl concept const consteval constexpr constinit const_cast continue co_await co_return co_yield decltype "
						"default delete do double dynamic_cast else enum explicit export extern false float for friend goto if inline "
						"int long mutable namespace new noexcept not not_eq nullptr operator or or_eq private protected public "
						"register reinterpret_cast requires return short signed sizeof static static_assert static_cast struct "
						"switch template this thread_local throw true try typedef typeid typename union unsigned using virtual void "
						"volatile wchar_t while xor xor_eq override final "
						"__attribute__ __asm__ __extension__ __restrict __restrict__ __inline __inline__ __typeof__ __builtin_expect";
				case 2:
					return "size_t ssize_t ptrdiff_t intptr_t uintptr_t int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t "
						"uint64_t pid_t off_t std";
			}
			return nullptr;
		}
	};

	class AsmLexer final : public QsciLexerMASM {
	public:
		using QsciLexerMASM::QsciLexerMASM;
		const char* keywords(int set) const override {
			switch (set) {
				case 1:		// CPU instructions
					return "mov movzx movsx movsxd lea push pop pushf pushfq popf popfq xchg cmpxchg cmpxchg8b cmpxchg16b "
						"xadd cwde cdq cdqe cqo cbw cwd bswap movsb movsw movsd movsq lodsb lodsw lodsd lodsq stosb stosw "
						"stosd stosq scasb scasw scasd scasq cmpsb cmpsw cmpsd cmpsq ins insb insw insd outs outsb outsw outsd "
						"add adc sub sbb mul imul div idiv inc dec neg cmp adcx adox "
						"and or xor not test shl shr sal sar rol ror rcl rcr shld shrd bt bts btr btc bsf bsr popcnt "
						"lzcnt tzcnt andn blsi blsr blsmsk "
						"jmp je jz jne jnz jg jnle jge jnl jl jnge jle jng ja jnbe jae jnb jb jnae jbe jna jc jnc jo jno "
						"js jns jp jpe jnp jpo jcxz jecxz jrcxz call ret retn retf int int3 into iret iretd iretq "
						"loop loope loopne loopz loopnz enter leave syscall sysenter sysexit sysret ud2 hlt nop pause "
						"endbr64 endbr32 lock rep repe repz repne repnz bnd notrack "
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
						"vpshufd vpcmpeqb vpcmpeqd vinsertf128 vextractf128 vbroadcastss vpbroadcastd vperm2f128 vpermq vpunpcklqdq";
				case 2:		// FPU instructions
					return "fld fst fstp fild fist fistp fadd fsub fsubr fmul fdiv fdivr fabs fchs fcom fcomp fcompp fucom "
						"fucomp fucompp fcomi fcomip fucomi fucomip fxch fnop ffree fld1 fldz fldpi fldl2e fldl2t fldlg2 "
						"fldln2 fscale fsqrt fsin fcos fsincos fptan fpatan f2xm1 fyl2x fyl2xp1 fnstsw fstsw fnstcw fstcw "
						"fldcw fnclex fclex fninit finit fwait wait frndint fxtract fprem fprem1 fdecstp fincstp "
						"fsave fnsave frstor fxam ftst";
				case 3:		// registers
					return "rax rbx rcx rdx rsi rdi rbp rsp r8 r9 r10 r11 r12 r13 r14 r15 rip "
						"eax ebx ecx edx esi edi ebp esp r8d r9d r10d r11d r12d r13d r14d r15d eip "
						"ax bx cx dx si di bp sp r8w r9w r10w r11w r12w r13w r14w r15w "
						"al bl cl dl ah bh ch dh sil dil bpl spl r8b r9b r10b r11b r12b r13b r14b r15b "
						"cs ds es fs gs ss eflags rflags "
						"cr0 cr1 cr2 cr3 cr4 cr8 dr0 dr1 dr2 dr3 dr6 dr7 "
						"xmm0 xmm1 xmm2 xmm3 xmm4 xmm5 xmm6 xmm7 xmm8 xmm9 xmm10 xmm11 xmm12 xmm13 xmm14 xmm15 "
						"ymm0 ymm1 ymm2 ymm3 ymm4 ymm5 ymm6 ymm7 ymm8 ymm9 ymm10 ymm11 ymm12 ymm13 ymm14 ymm15 "
						"mm0 mm1 mm2 mm3 mm4 mm5 mm6 mm7 st0 st1 st2 st3 st4 st5 st6 st7";
			}
			return nullptr;
		}
	};

	// the language of a source file by its name (no extension: a C++ standard library header)
	CodeEditor::Language LanguageOf(const QString& path) {
		static const QStringList cpp = { "c", "cc", "cpp", "cxx", "c++", "h", "hh", "hpp", "hxx", "h++", "inl", "ipp", "tcc", "ixx", "cppm", "" };
		return cpp.contains(QFileInfo(path).suffix().toLower()) ? CodeEditor::Language::Cpp : CodeEditor::Language::Text;
	}
}

//
// CodeEditor
//

CodeEditor::CodeEditor(Language language, QWidget* parent) : QsciScintilla(parent), m_Language(language) {
	setUtf8(true);
	setReadOnly(true);
	setWrapMode(WrapNone);
	setTabWidth(4);
	SendScintilla(SCI_SETSCROLLWIDTH, 1ul);
	SendScintilla(SCI_SETSCROLLWIDTHTRACKING, 1ul);
	setContextMenuPolicy(Qt::DefaultContextMenu);

	if (language == Language::Cpp) {
		m_Lexer = new CppLexer(this);
		setLexer(m_Lexer);
		// (what is defined is not known here: code in an #if the lexer would take for inactive is shown as any other)
		SendScintilla(SCI_SETPROPERTY, "lexer.cpp.track.preprocessor", "0");
	}
	else if (language == Language::Asm) {
		m_Lexer = new AsmLexer(this);
		setLexer(m_Lexer);
	}

	setMarginType(MarginNumbers, NumberMargin);
	setMarginMarkerMask(MarginNumbers, 0);
	setMarginSensitivity(MarginNumbers, true);
	setMarginType(MarginSymbols, SymbolMargin);
	setMarginWidth(MarginSymbols, 18);
	setMarginMarkerMask(MarginSymbols, (1 << MarkerBreakpoint) | (1 << MarkerBreakpointDisabled) | (1 << MarkerCurrent) | (1 << MarkerFrame));
	setMarginSensitivity(MarginSymbols, true);
	markerDefine(Circle, MarkerBreakpoint);
	markerDefine(Circle, MarkerBreakpointDisabled);
	markerDefine(RightArrow, MarkerCurrent);
	markerDefine(RightArrow, MarkerFrame);
	// the whole line, behind the text
	markerDefine(Background, MarkerCurrentLine);
	markerDefine(Background, MarkerFrameLine);

	connect(this, &QsciScintilla::marginClicked, this, [this](int, int line, Qt::KeyboardModifiers) { emit GutterClicked(line); });
	connect(this, &QsciScintilla::linesChanged, this, [this] { UpdateNumberWidth(); });
	ApplyFont();
	ApplyColors();
}

void CodeEditor::ApplyFont() {
	auto font = CodeFont();
	if (m_Lexer) {
		m_Lexer->setDefaultFont(font);
		m_Lexer->setFont(font, -1);
		if (m_Language == Language::Asm) {
			auto bold = font;
			bold.setBold(true);
			for (auto style : { QsciLexerAsm::CPUInstruction, QsciLexerAsm::FPUInstruction, QsciLexerAsm::ExtendedInstruction })
				m_Lexer->setFont(bold, style);
		}
	}
	else
		setFont(font);
	setMarginsFont(font);
	UpdateNumberWidth();
}

void CodeEditor::UpdateNumberWidth() {
	if (!m_ShowLineNumbers) {
		setMarginWidth(MarginNumbers, 0);
		return;
	}
	// (the widest number, and a little room)
	setMarginWidth(MarginNumbers, QString(QString::number(std::max(lines(), 1)).length() + 1, '9'));
}

void CodeEditor::changeEvent(QEvent* event) {
	QsciScintilla::changeEvent(event);
	if (event->type() == QEvent::PaletteChange)
		ApplyColors();
}

void CodeEditor::ApplyColors() {
	const bool dark = IsDarkPalette();
	auto palette = QPalette();
	auto fore = palette.color(QPalette::Text), back = palette.color(QPalette::Base);
	if (m_Lexer) {
		m_Lexer->setDefaultColor(fore);
		m_Lexer->setDefaultPaper(back);
		m_Lexer->setColor(fore, -1);
		m_Lexer->setPaper(back, -1);
	}
	else {
		setColor(fore);
		setPaper(back);
	}
	setMarginsForegroundColor(palette.color(QPalette::Disabled, QPalette::Text));
	setMarginsBackgroundColor(back);
	setFoldMarginColors(back, back);
	setCaretForegroundColor(fore);
	setSelectionBackgroundColor(palette.color(QPalette::Highlight));
	setSelectionForegroundColor(palette.color(QPalette::HighlightedText));

	QColor red = dark ? QColor(200, 60, 60) : QColor(220, 0, 0);
	setMarkerForegroundColor(Qt::white, MarkerBreakpoint);
	setMarkerBackgroundColor(red, MarkerBreakpoint);
	setMarkerForegroundColor(red, MarkerBreakpointDisabled);
	setMarkerBackgroundColor(back, MarkerBreakpointDisabled);
	setMarkerForegroundColor(Qt::black, MarkerCurrent);
	setMarkerBackgroundColor(QColor(255, 230, 0), MarkerCurrent);
	setMarkerForegroundColor(Qt::black, MarkerFrame);
	setMarkerBackgroundColor(QColor(80, 200, 80), MarkerFrame);
	setMarkerBackgroundColor(dark ? QColor(74, 68, 24) : QColor(255, 247, 190), MarkerCurrentLine);
	setMarkerBackgroundColor(dark ? QColor(30, 64, 36) : QColor(214, 240, 214), MarkerFrameLine);

	auto set = [&](std::initializer_list<int> styles, QColor light, QColor inDark) {
		for (auto style : styles)
			m_Lexer->setColor(dark ? inDark : light, style);
	};
	if (m_Language == Language::Cpp) {
		set({ QsciLexerCPP::Comment, QsciLexerCPP::CommentLine, QsciLexerCPP::CommentDoc, QsciLexerCPP::CommentLineDoc,
			QsciLexerCPP::CommentDocKeyword, QsciLexerCPP::CommentDocKeywordError }, QColor(0, 128, 0), QColor(87, 166, 74));
		set({ QsciLexerCPP::Number }, QColor(9, 134, 88), QColor(181, 206, 168));
		set({ QsciLexerCPP::Keyword }, QColor(0, 0, 255), QColor(86, 156, 214));
		set({ QsciLexerCPP::KeywordSet2 }, QColor(43, 145, 175), QColor(78, 201, 176));
		set({ QsciLexerCPP::DoubleQuotedString, QsciLexerCPP::SingleQuotedString, QsciLexerCPP::VerbatimString, QsciLexerCPP::RawString,
			QsciLexerCPP::EscapeSequence }, QColor(163, 21, 21), QColor(214, 157, 133));
		set({ QsciLexerCPP::UnclosedString }, QColor(255, 0, 0), QColor(255, 120, 120));
		set({ QsciLexerCPP::PreProcessor }, QColor(128, 128, 128), QColor(155, 155, 155));
		set({ QsciLexerCPP::Operator }, fore, QColor(180, 180, 180));
		// (the lexer's own: an unclosed string has a background)
		m_Lexer->setPaper(back, QsciLexerCPP::UnclosedString);
		m_Lexer->setEolFill(false, QsciLexerCPP::UnclosedString);
	}
	else if (m_Language == Language::Asm) {
		set({ QsciLexerAsm::Comment, QsciLexerAsm::BlockComment, QsciLexerAsm::CommentDirective }, QColor(0, 128, 0), QColor(96, 180, 96));
		set({ QsciLexerAsm::Number }, QColor(9, 134, 88), QColor(181, 206, 168));
		set({ QsciLexerAsm::DoubleQuotedString, QsciLexerAsm::SingleQuotedString }, QColor(163, 21, 21), QColor(214, 157, 133));
		set({ QsciLexerAsm::UnclosedString }, QColor(255, 0, 0), QColor(255, 120, 120));
		set({ QsciLexerAsm::Register }, QColor(0, 128, 128), QColor(78, 201, 176));
		set({ QsciLexerAsm::Directive, QsciLexerAsm::DirectiveOperand }, QColor(128, 0, 128), QColor(197, 134, 192));
		set({ QsciLexerAsm::CPUInstruction, QsciLexerAsm::FPUInstruction, QsciLexerAsm::ExtendedInstruction }, QColor(0, 0, 255), QColor(86, 156, 214));
		m_Lexer->setPaper(back, QsciLexerAsm::UnclosedString);
		m_Lexer->setEolFill(false, QsciLexerAsm::UnclosedString);
	}
}

void CodeEditor::SetShowLineNumbers(bool show) {
	m_ShowLineNumbers = show;
	UpdateNumberWidth();
}

void CodeEditor::SetBreakpointLines(std::set<int> lines) {
	m_Breakpoints = std::move(lines);
	UpdateMarkers();
}

void CodeEditor::SetDisabledBreakpointLines(std::set<int> lines) {
	m_DisabledBreakpoints = std::move(lines);
	UpdateMarkers();
}

void CodeEditor::SetCurrentLine(int line, bool scope) {
	m_CurrentLine = line;
	m_CurrentIsScope = scope;
	UpdateMarkers();
}

void CodeEditor::UpdateMarkers() {
	for (int marker = MarkerBreakpoint; marker <= MarkerFrameLine; marker++)
		markerDeleteAll(marker);
	int count = lines();
	for (int line : m_Breakpoints)
		if (line < count)
			markerAdd(line, m_DisabledBreakpoints.contains(line) ? MarkerBreakpointDisabled : MarkerBreakpoint);
	if (m_CurrentLine >= 0 && m_CurrentLine < count) {
		markerAdd(m_CurrentLine, m_CurrentIsScope ? MarkerFrame : MarkerCurrent);
		markerAdd(m_CurrentLine, m_CurrentIsScope ? MarkerFrameLine : MarkerCurrentLine);
	}
}

void CodeEditor::ShowLine(int line) {
	if (line < 0 || line >= lines())
		return;
	int first = firstVisibleLine();
	int onScreen = static_cast<int>(SendScintilla(SCI_LINESONSCREEN));
	bool inView = line >= first && line < first + onScreen - 1;
	setCursorPosition(line, 0);
	if (!inView)
		setFirstVisibleLine(std::max(0, line - onScreen / 2));
}

int CodeEditor::CaretLine() const {
	int line, index;
	getCursorPosition(&line, &index);
	return line;
}

int CodeEditor::LineAt(const QPoint& pos) const {
	auto position = SendScintilla(SCI_POSITIONFROMPOINT, static_cast<unsigned long>(pos.x()), static_cast<long>(pos.y()));
	return static_cast<int>(SendScintilla(SCI_LINEFROMPOSITION, static_cast<unsigned long>(position)));
}

//
// SourceView
//

SourceView::SourceView(const QString& path, QWidget* parent) : CodeEditor(LanguageOf(path), parent), m_Path(path) {
	connect(this, &CodeEditor::GutterClicked, this, [this](int line) {
		MainWindow::Get().ToggleSourceBreakpoint(this, line);
	});
	// the value of what the mouse rests on, while the target is stopped
	SendScintilla(SCI_SETMOUSEDWELLTIME, 500ul);
	connect(this, &QsciScintillaBase::SCN_DWELLSTART, this, [this](int position, int x, int y) {
		if (position < 0 || !Session().HasTarget() || !Session().IsBroken())
			return;
		auto expression = ExpressionAt(position);
		if (!expression.isEmpty())
			ShowValueTip(expression, viewport()->mapToGlobal(QPoint(x, y)));
	});
	connect(this, &QsciScintillaBase::SCN_DWELLEND, this, [] { QToolTip::hideText(); });
}

bool SourceView::Load() {
	QFile file(m_Path);
	if (!file.open(QIODevice::ReadOnly))
		return false;
	setText(QString::fromUtf8(file.readAll()));
	return true;
}

QString SourceView::ExpressionAt(int position) const {
	// in the selection: the selection
	auto start = static_cast<int>(SendScintilla(SCI_GETSELECTIONSTART)), end = static_cast<int>(SendScintilla(SCI_GETSELECTIONEND));
	if (start < end && position >= start && position <= end)
		return selectedText().trimmed();
	// (Scintilla's positions are bytes of UTF-8; a name is ASCII here)
	auto line = static_cast<int>(SendScintilla(SCI_LINEFROMPOSITION, static_cast<unsigned long>(position)));
	auto text = this->text(line).toUtf8();
	int at = position - static_cast<int>(SendScintilla(SCI_POSITIONFROMLINE, static_cast<unsigned long>(line)));
	auto isName = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; };
	if (at < 0 || at >= text.length() || !isName(text[at]))
		return {};
	int last = at;
	while (last < text.length() && isName(text[last]))
		last++;
	// with what it is a member of: a.b, p->b, this->b
	int first = at;
	for (;;) {
		while (first > 0 && isName(text[first - 1]))
			first--;
		if (first > 0 && text[first - 1] == '.')
			first--;
		else if (first > 1 && text[first - 1] == '>' && text[first - 2] == '-')
			first -= 2;
		else
			break;
	}
	auto expression = QString::fromUtf8(text.mid(first, last - first));
	if (expression.isEmpty() || expression[0].isDigit())
		return {};
	return expression;
}

Task<void> SourceView::ShowValueTip(QString expression, QPoint where) {
	auto alive = m_Lifetime.Watch();
	std::vector<SymbolEntry> entries;
	co_await Debugger().EvaluateWatches(entries, { W(expression) }, {});
	if (!*alive || entries.empty() || entries[0].Error)
		co_return;
	QToolTip::showText(where, QStringLiteral("%1 = %2").arg(expression, Q(entries[0].Value)), this);
}

void SourceView::contextMenuEvent(QContextMenuEvent* event) {
	auto& main = MainWindow::Get();
	auto pos = event->reason() == QContextMenuEvent::Mouse ? event->pos() : QPoint(0, 0);
	auto position = event->reason() == QContextMenuEvent::Mouse
		? static_cast<int>(SendScintilla(SCI_POSITIONFROMPOINT, static_cast<unsigned long>(pos.x()), static_cast<long>(pos.y())))
		: static_cast<int>(SendScintilla(SCI_GETCURRENTPOS));
	if (!hasSelectedText())
		SendScintilla(SCI_GOTOPOS, static_cast<unsigned long>(position));
	auto line = static_cast<int>(SendScintilla(SCI_LINEFROMPOSITION, static_cast<unsigned long>(position)));
	bool canRun = Session().CanRun();
	QMenu menu;
	menu.addAction(tr("Toggle &Breakpoint"), QKeySequence(Qt::Key_F9), this, [this, line] { MainWindow::Get().ToggleSourceBreakpoint(this, line); });
	menu.addAction(tr("Run to &Cursor"), QKeySequence(Qt::CTRL | Qt::Key_F10), this, [this, line] { MainWindow::Get().RunToSourceLine(this, line); })->setEnabled(canRun);
	menu.addAction(tr("Set &Next Statement"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F10), this, [this, line] { MainWindow::Get().SetNextStatementToLine(this, line); })->setEnabled(canRun);
	auto expression = ExpressionAt(position);
	if (!expression.isEmpty()) {
		menu.addSeparator();
		menu.addAction(tr("Add &Watch: %1").arg(expression), &main, [&main, expression] { main.AddWatch(expression); });
	}
	menu.addSeparator();
	menu.addAction(tr("&Copy"), QKeySequence::Copy, this, &QsciScintilla::copy)->setEnabled(hasSelectedText());
	menu.addAction(tr("Select &All"), QKeySequence::SelectAll, this, [this] { selectAll(); });
	menu.exec(event->globalPos());
}
