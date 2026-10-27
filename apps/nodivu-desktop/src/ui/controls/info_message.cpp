#include "ui/controls/info_message.hpp"
#include <QAbstractTextDocumentLayout>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <algorithm>
#include <cmath>

namespace nodivu {
InfoMessage::InfoMessage(bool compact, QWidget* parent) : QTextEdit(parent), compact_(compact) {
    setStyleSheet(QStringLiteral("QTextEdit { color: #f0d49b; background: #342e23; border: 0; "
                                 "border-radius: 6px; padding: %1px; font-size: %2px; }")
                      .arg(compact ? 6 : 9)
                      .arg(compact ? 9 : 12));
    setReadOnly(true);
    setAcceptRichText(false);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::ArrowCursor);
    viewport()->setCursor(Qt::ArrowCursor);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    document()->setDocumentMargin(0);
    // Measure after native wrapping at the viewport's actual width.
    connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this,
            [this](const QSizeF& size) { updateHeight(size.height()); });
}
void InfoMessage::setMessage(const QString& text) {
    if (toPlainText() != text) {
        setPlainText(text);
        QTextCursor cursor(document());
        cursor.select(QTextCursor::Document);
        QTextBlockFormat format;
        format.setLineHeight((compact_ ? 9 : 12) * 1.5, QTextBlockFormat::FixedHeight);
        cursor.mergeBlockFormat(format);
    }
    updateHeight(document()->size().height());
}
void InfoMessage::updateHeight(qreal documentHeight) {
    const int padding = compact_ ? 12 : 18;
    const int lineHeight = compact_ ? 14 : 18;
    const int wanted = static_cast<int>(std::ceil(documentHeight)) + padding;
    const int bounded = std::clamp(wanted, padding + lineHeight, compact_ ? 60 : 160);
    if (height() != bounded) {
        setFixedHeight(bounded);
        emit geometryChanged();
    }
}
} // namespace nodivu
