#pragma once

#include "PaneBase.h"

class QLineEdit;
class QPlainTextEdit;

//
// Memory (CMemoryView): bytes at an address *expression* ($sp, &var, ...), evaluated again on each stop so that it can
// follow a register; 16 bytes a line with their characters. The bytes that changed since the last refresh of the same
// address are red. Write Bytes (the right-click menu) writes to a live target that is stopped.
//
class MemoryView final : public PaneBase {
public:
	explicit MemoryView(QWidget* parent = nullptr);

	bool GoTo() override;
	QString Expression() const;
	void SetExpression(const QString& expression);

private:
	DebugEngine::Task<void> Refresh() override;
	void Clear() override;
	DebugEngine::Task<void> WriteBytes();
	void contextMenuEvent(QContextMenuEvent* event) override;

	QLineEdit* m_Address;
	QPlainTextEdit* m_Text;
	ULONG64 m_Shown{ 0 };
	std::vector<BYTE> m_Bytes;
	static constexpr ULONG Size = 512;
};
