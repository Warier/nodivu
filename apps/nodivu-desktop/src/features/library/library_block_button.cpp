#include "features/library/library_block_button.hpp"
#include "ui/theme/theme.hpp"

#include <QApplication>
#include <QDrag>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <algorithm>

namespace nodivu {

LibraryBlockButton::LibraryBlockButton(const NodeDescriptor& descriptor, QWidget* parent)
    : QPushButton(descriptor.title, parent), descriptor_(descriptor) {
    setObjectName(descriptor.catalogKey());
    setAccessibleName(descriptor.title);
    setAccessibleDescription(descriptor.subtitle + QLatin1Char('\n') + descriptor.description);
    setToolTip(descriptor.title +
               QStringLiteral(" · arraste para o projeto ou clique para adicionar"));
    setCursor(Qt::OpenHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumWidth(0);
    setAutoDefault(false);
    setFixedHeight(54);
}

void LibraryBlockButton::setCompact(bool compact) {
    compact_ = compact;
    setFixedHeight(compact ? 42 : 54);
    update();
}

QSize LibraryBlockButton::sizeHint() const {
    return {196, compact_ ? 42 : 54};
}

QSize LibraryBlockButton::minimumSizeHint() const {
    return {0, compact_ ? 42 : 54};
}

void LibraryBlockButton::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    if (!isEnabled())
        painter.setOpacity(0.38);
    const QRectF card = QRectF(rect()).adjusted(3.5, 0.5, -3.5, -0.5);
    QLinearGradient background(card.topLeft(), card.bottomRight());
    background.setColorAt(0, QColor(34, 45, 56));
    background.setColorAt(1, QColor(28, 36, 46));
    painter.setBrush(underMouse() ? QBrush(QColor(53, 65, 83)) : QBrush(background));
    painter.setPen(hasFocus()     ? Theme::mint()
                   : underMouse() ? QColor(101, 116, 138)
                                  : QColor(57, 69, 83));
    painter.drawRoundedRect(card, 7, 7);
    QFont font = this->font();
    font.setPixelSize(19);
    painter.setFont(font);
    painter.setPen(Theme::mint());
    painter.drawText(compact_ ? card : QRectF(card.left() + 24, card.top(), 22, card.height()),
                     Qt::AlignCenter, descriptor_.icon());
    if (!compact_) {
        font.setPixelSize(12);
        painter.setFont(font);
        painter.setPen(QColor(122, 137, 156));
        painter.drawText(QRectF(card.left() + 9, card.top(), 10, card.height()), Qt::AlignCenter,
                         QStringLiteral("⠿"));
        const qreal labelLeft = card.left() + 54;
        const qreal labelWidth = std::max(0.0, card.width() - 82);
        font.setPixelSize(11);
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        painter.setPen(QColor(228, 232, 238));
        painter.drawText(QRectF(labelLeft, card.top() + 11, labelWidth, 15), Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(descriptor_.title, Qt::ElideRight,
                                                          static_cast<int>(labelWidth)));
        font.setPixelSize(8);
        font.setWeight(QFont::Normal);
        painter.setFont(font);
        painter.setPen(Theme::muted());
        painter.drawText(QRectF(labelLeft, card.top() + 30, labelWidth, 11), Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(descriptor_.subtitle, Qt::ElideRight,
                                                          static_cast<int>(labelWidth)));
        font.setPixelSize(16);
        painter.setFont(font);
        painter.setPen(QColor(228, 232, 238));
        painter.drawText(QRectF(card.right() - 22, card.top(), 12, card.height()), Qt::AlignCenter,
                         QStringLiteral("+"));
    }
    painter.setBrush(Theme::headerBackground());
    const qreal portY = compact_ ? card.center().y() : card.top() + 24;
    if (descriptor_.inputs > 0) {
        painter.setPen(QColor(165, 144, 210));
        painter.drawEllipse(QPointF(card.left(), portY), 3, 3);
    }
    if (descriptor_.outputs > 0) {
        painter.setPen(Theme::mint());
        painter.drawEllipse(QPointF(card.right(), portY), 3, 3);
    }
}

void LibraryBlockButton::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        pressPosition_ = event->position().toPoint();
        dragged_ = false;
    }
    QPushButton::mousePressEvent(event);
}

void LibraryBlockButton::mouseMoveEvent(QMouseEvent* event) {
    if (!isEnabled() || !event->buttons().testFlag(Qt::LeftButton) || dragged_ ||
        (event->position().toPoint() - pressPosition_).manhattanLength() <
            QApplication::startDragDistance()) {
        QPushButton::mouseMoveEvent(event);
        return;
    }
    dragged_ = true;
    setDown(false);
    auto* mime = new QMimeData;
    mime->setData(QStringLiteral("application/x-nodivu-block"),
                  QJsonDocument(QJsonObject{{QStringLiteral("kind"), descriptor_.kind},
                                            {QStringLiteral("plugin_id"), descriptor_.pluginId}})
                      .toJson(QJsonDocument::Compact));
    QPointer<QDrag> drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->setPixmap(grab());
    drag->setHotSpot(pressPosition_);
    drag->exec(Qt::CopyAction);
    // A catalog refresh may destroy the source while the native drag is running.
    if (drag)
        drag->deleteLater();
}

void LibraryBlockButton::mouseReleaseEvent(QMouseEvent* event) {
    if (dragged_) {
        dragged_ = false;
        setDown(false);
        event->accept();
        return;
    }
    QPushButton::mouseReleaseEvent(event);
}

void LibraryBlockButton::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        click();
        event->accept();
        return;
    }
    QPushButton::keyPressEvent(event);
}
} // namespace nodivu
