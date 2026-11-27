#include "Dialogs.h"
#include "Common.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <fstream>

using namespace DebugEngine;

namespace {
	QDialogButtonBox* OkCancel(QDialog* dialog) {
		auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
		QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
		QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
		return buttons;
	}

	// a form row whose field is a layout: QFormLayout gives its label no buddy then, and a label without one shows the '&'
	void AddRow(QFormLayout* form, const QString& text, QWidget* buddy, QLayout* field) {
		auto label = new QLabel(text, buddy->parentWidget());
		label->setBuddy(buddy);
		form->addRow(label, field);
	}

	QHBoxLayout* WithBrowse(QLineEdit* edit, QWidget* parent, std::function<void()> browse) {
		auto row = new QHBoxLayout;
		row->addWidget(edit);
		auto button = new QPushButton(QObject::tr("Browse..."), parent);
		QObject::connect(button, &QPushButton::clicked, parent, browse);
		row->addWidget(button);
		return row;
	}
}

QString ResolveProgram(const QString& typed) {
	auto text = typed.trimmed();
	if (text.isEmpty())
		return {};
	if (text == "~" || text.startsWith("~/"))
		text = QDir::homePath() + text.mid(1);
	if (!text.contains('/'))
		return QStandardPaths::findExecutable(text);
	QFileInfo info(text);
	return info.isFile() && info.isExecutable() ? info.absoluteFilePath() : QString();
}

//
// Run Executable
//

RunExecutableDialog::RunExecutableDialog(QWidget* parent) : QDialog(parent) {
	setWindowTitle(tr("Run Executable"));
	resize(560, 0);
	auto form = new QFormLayout;
	m_Program = new QLineEdit(this);
	m_Arguments = new QLineEdit(this);
	m_Folder = new QLineEdit(this);
	m_Folder->setPlaceholderText(tr("(the program's folder)"));
	m_Program->setPlaceholderText(tr("A path, or a program in PATH"));
	AddRow(form, tr("&Program:"), m_Program, WithBrowse(m_Program, this, [this] {
		auto file = QFileDialog::getOpenFileName(this, tr("Program"), QFileInfo(Program()).absolutePath());
		if (!file.isEmpty())
			m_Program->setText(file);
	}));
	form->addRow(tr("&Arguments:"), m_Arguments);
	AddRow(form, tr("&Working folder:"), m_Folder, WithBrowse(m_Folder, this, [this] {
		auto folder = QFileDialog::getExistingDirectory(this, tr("Working Folder"), m_Folder->text());
		if (!folder.isEmpty())
			m_Folder->setText(folder);
	}));
	auto layout = new QVBoxLayout(this);
	layout->addLayout(form);
	auto buttons = OkCancel(this);
	layout->addWidget(buttons);
	auto ok = buttons->button(QDialogButtonBox::Ok);
	ok->setEnabled(false);
	connect(m_Program, &QLineEdit::textChanged, this, [ok](const QString& text) {
		ok->setEnabled(!ResolveProgram(text).isEmpty());
	});
}

void RunExecutableDialog::SetValues(const QString& program, const QString& arguments, const QString& folder) {
	m_Program->setText(program);
	m_Arguments->setText(arguments);
	m_Folder->setText(folder);
}

QString RunExecutableDialog::Program() const {
	auto program = ResolveProgram(m_Program->text());
	return program.isEmpty() ? m_Program->text().trimmed() : program;
}

QString RunExecutableDialog::Arguments() const {
	return m_Arguments->text();
}

QString RunExecutableDialog::Folder() const {
	return m_Folder->text().isEmpty() ? QFileInfo(Program()).absolutePath() : m_Folder->text();
}

//
// Attach to Process
//

AttachDialog::AttachDialog(const std::vector<ServerProcess>& processes, QWidget* parent) : QDialog(parent) {
	setWindowTitle(tr("Attach to Process"));
	resize(640, 480);
	auto layout = new QVBoxLayout(this);
	m_Filter = new QLineEdit(this);
	m_Filter->setPlaceholderText(tr("Filter"));
	m_Filter->setClearButtonEnabled(true);
	layout->addWidget(m_Filter);
	m_List = new QTreeWidget(this);
	m_List->setHeaderLabels({ tr("PID"), tr("Name"), tr("Command Line") });
	m_List->setRootIsDecorated(false);
	m_List->setUniformRowHeights(true);
	m_List->setSortingEnabled(true);
	auto self = static_cast<ULONG>(QCoreApplication::applicationPid());
	for (auto& p : processes) {
		if (p.Pid == self)
			continue;
		std::ifstream cmdline("/proc/" + std::to_string(p.Pid) + "/cmdline");
		std::string args((std::istreambuf_iterator<char>(cmdline)), {});
		std::replace(args.begin(), args.end(), '\0', ' ');
		auto item = new QTreeWidgetItem(m_List, { QString::number(p.Pid), Q(p.Name), QString::fromStdString(args).trimmed() });
		item->setData(0, Qt::DisplayRole, p.Pid);
	}
	m_List->sortByColumn(1, Qt::AscendingOrder);
	m_List->header()->resizeSection(1, 180);
	layout->addWidget(m_List);
	auto buttons = OkCancel(this);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("&Attach"));
	layout->addWidget(buttons);
	connect(m_Filter, &QLineEdit::textChanged, this, [this](const QString& text) {
		for (int i = 0; i < m_List->topLevelItemCount(); i++) {
			auto item = m_List->topLevelItem(i);
			item->setHidden(!text.isEmpty() && !item->text(0).contains(text) && !item->text(1).contains(text, Qt::CaseInsensitive) &&
				!item->text(2).contains(text, Qt::CaseInsensitive));
		}
	});
	connect(m_List, &QTreeWidget::itemDoubleClicked, this, &QDialog::accept);
	m_Filter->setFocus();
}

ULONG AttachDialog::Pid() const {
	auto item = m_List->currentItem();
	return item && !item->isHidden() ? item->text(0).toUInt() : 0;
}

QString AttachDialog::Name() const {
	auto item = m_List->currentItem();
	return item ? item->text(1) : QString();
}

//
// Add Breakpoint
//

BreakpointDialog::BreakpointDialog(QWidget* parent) : QDialog(parent) {
	setWindowTitle(tr("Add Breakpoint"));
	resize(520, 0);
	auto form = new QFormLayout;
	m_Location = new QLineEdit(this);
	m_Location->setPlaceholderText(tr("function, file.cpp:12, *0x401000 (data: an address expression, &variable)"));
	m_Condition = new QLineEdit(this);
	m_Condition->setPlaceholderText(tr("a C++ expression; empty: always"));
	m_Command = new QLineEdit(this);
	m_Command->setPlaceholderText(tr("GDB commands to run when it breaks"));
	m_PassCount = new QLineEdit(QStringLiteral("1"), this);
	m_Data = new QCheckBox(tr("&Memory access (a data breakpoint)"), this);
	m_Size = new QComboBox(this);
	m_Size->addItems({ "1", "2", "4", "8" });
	m_Size->setCurrentIndex(2);
	m_Access = new QComboBox(this);
	m_Access->addItems({ tr("Write"), tr("Read or write"), tr("Execute") });
	m_OneShot = new QCheckBox(tr("&One-shot (removed when hit)"), this);
	form->addRow(tr("&Location:"), m_Location);
	form->addRow(tr("&Condition:"), m_Condition);
	form->addRow(tr("Co&mmands:"), m_Command);
	form->addRow(tr("Break on &hit:"), m_PassCount);
	form->addRow(QString(), m_OneShot);
	form->addRow(QString(), m_Data);
	form->addRow(tr("&Size:"), m_Size);
	form->addRow(tr("&Access:"), m_Access);
	auto update = [this] {
		m_Size->setEnabled(m_Data->isChecked());
		m_Access->setEnabled(m_Data->isChecked());
	};
	connect(m_Data, &QCheckBox::toggled, this, update);
	update();
	auto layout = new QVBoxLayout(this);
	layout->addLayout(form);
	layout->addWidget(OkCancel(this));
}

BreakpointSettings BreakpointDialog::Settings() const {
	BreakpointSettings s;
	s.Expression = W(m_Location->text().trimmed());
	s.Condition = W(m_Condition->text().trimmed());
	s.Command = W(m_Command->text().trimmed());
	s.PassCount = std::max(1u, m_PassCount->text().toUInt());
	s.OneShot = m_OneShot->isChecked();
	s.IsData = m_Data->isChecked();
	s.DataSize = m_Size->currentText().toUInt();
	s.DataAccess = m_Access->currentIndex() == 0 ? BreakOnWrite : m_Access->currentIndex() == 1 ? BreakOnRead : BreakOnExecute;
	return s;
}

//
// Symbol and Source Paths
//

PathsDialog::PathsDialog(const QString& symbolPath, const QString& sourcePath, QWidget* parent) : QDialog(parent) {
	setWindowTitle(tr("Symbol and Source Paths"));
	resize(560, 420);
	auto layout = new QVBoxLayout(this);
	layout->addWidget(new QLabel(tr("Folders of &separate debug files (GDB's debug-file-directory), one a line:"), this));
	m_Symbols = new QPlainTextEdit(this);
	m_Symbols->setPlainText(QString(symbolPath).replace(':', '\n'));
	layout->addWidget(m_Symbols);
	layout->addWidget(new QLabel(tr("Folders to look for &source files in (besides where they were compiled), one a line:"), this));
	m_Sources = new QPlainTextEdit(this);
	auto sources = QString(sourcePath).split(':', Qt::SkipEmptyParts);
	sources.removeAll("$cdir");
	sources.removeAll("$cwd");
	m_Sources->setPlainText(sources.join('\n'));
	layout->addWidget(m_Sources);
	layout->addWidget(OkCancel(this));
}

QString PathsDialog::SymbolPath() const {
	return m_Symbols->toPlainText().split('\n', Qt::SkipEmptyParts).join(':');
}

QString PathsDialog::SourcePath() const {
	return m_Sources->toPlainText().split('\n', Qt::SkipEmptyParts).join(':');
}
