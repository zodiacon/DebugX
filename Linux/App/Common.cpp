#include "Common.h"
#include <QApplication>
#include <QFontDatabase>
#include <QSettings>
#include <QWidget>

QString HexAddress(ULONG64 address) {
	return QStringLiteral("%1").arg(address, 16, 16, QLatin1Char('0'));
}

QFont CodeFont() {
	QFont font;
	auto saved = QSettings().value("codeFont").toString();
	if (saved.isEmpty() || !font.fromString(saved)) {
		font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
		font.setPointSize(std::max(font.pointSize(), 10));
	}
	return font;
}

void SetCodeFont(const QFont& font) {
	QSettings().setValue("codeFont", font.toString());
}
