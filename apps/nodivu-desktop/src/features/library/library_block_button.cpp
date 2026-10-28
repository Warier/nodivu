#include "features/library/library_block_button.hpp"
#include "ui/theme/studio_icons.hpp"
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
#include <QResizeEvent>
#include <algorithm>
#include <cmath>

namespace nodivu {
namespace {
qreal layoutText(QTextLayout& layout, qreal width) {
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);
    layout.beginLayout();
    qreal height = 0;
    while (true) {
        auto line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(std::max(1.0, width));
        line.setPosition(QPointF(0, height));
        height += line.height();
    }
    layout.endLayout();
    return height;
}
QString libraryDescription(const NodeDescriptor& descriptor) {
    if (descriptor.kind == QStringLiteral("capture"))
        return QStringLiteral(
            "Traz o áudio do microfone ou da entrada selecionada para o projeto.");
    if (descriptor.kind == QStringLiteral("output"))
        return QStringLiteral(
            "Envia o áudio conectado para seus fones, caixas ou microfone virtual.");
    if (descriptor.kind == QStringLiteral("gain"))
        return QStringLiteral("Aumenta ou reduz o volume do áudio que passa pelo bloco.");
    if (descriptor.kind == QStringLiteral("tone"))
        return QStringLiteral("Gera um som de teste para conferir as conexões e a saída de áudio.");
    if (descriptor.kind == QStringLiteral("mixer"))
        return QStringLiteral("Combina até quatro entradas de áudio em uma única saída.");
    if (descriptor.kind == QStringLiteral("meter"))
        return QStringLiteral("Mostra o nível do áudio recebido, sem enviar o som adiante.");
    if (descriptor.filePlayer)
        return QStringLiteral("Reproduz um arquivo de áudio e envia o som aos blocos conectados.");
    if (descriptor.captureSource)
        return QStringLiteral("Traz o som de um aplicativo ou do PC para o projeto.");
    const auto description = descriptor.description.simplified();
    if (!description.isEmpty()) {
        const auto sentenceEnd = description.indexOf(QStringLiteral(". "));
        return sentenceEnd < 0 ? description : description.left(sentenceEnd + 1);
    }
    if (descriptor.inputs == 0)
        return QStringLiteral("Fornece áudio para os blocos conectados no projeto.");
    if (descriptor.outputs == 0)
        return QStringLiteral("Recebe o áudio dos blocos conectados no projeto.");
    return QStringLiteral("Processa o áudio recebido antes de enviá-lo aos próximos blocos.");
}
} // namespace

LibraryBlockButton::LibraryBlockButton(const NodeDescriptor& descriptor, QWidget* parent)
    : QPushButton(descriptor.title, parent), descriptor_(descriptor),
      titleLayout_(descriptor.title), subtitleLayout_(descriptor.subtitle) {
    setObjectName(descriptor.catalogKey());
    setAccessibleName(descriptor.title);
    setAccessibleDescription(descriptor.subtitle + QLatin1Char('\n') + descriptor.description);
    const auto instruction =
        descriptor.title + QStringLiteral(" · arraste para o projeto ou clique para adicionar");
    setToolTip(
        QStringLiteral("<qt>%1<br><br>%2</qt>")
            .arg(instruction.toHtmlEscaped(), libraryDescription(descriptor).toHtmlEscaped()));
    setCursor(Qt::OpenHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumWidth(0);
    setAutoDefault(false);
    updateTextLayout(196);
}

void LibraryBlockButton::setCompact(bool compact) {
    compact_ = compact;
    updateTextLayout(std::max(132, width()));
    update();
}

QSize LibraryBlockButton::sizeHint() const {
    return {196, height()};
}

QSize LibraryBlockButton::minimumSizeHint() const {
    return {0, height()};
}
void LibraryBlockButton::updateTextLayout(int width) {
    if (compact_) {
        setFixedHeight(42);
        return;
    }
    narrow_ = width < 200;
    labelLeft_ = narrow_ ? 40 : 54;
    const qreal available = std::max(1, width - 7 - (narrow_ ? 66 : 82));
    auto font = this->font();
    font.setPixelSize(Theme::BodyFont);
    font.setWeight(QFont::DemiBold);
    titleLayout_.setFont(font);
    titleHeight_ = layoutText(titleLayout_, available);
    font.setPixelSize(Theme::DetailFont);
    font.setWeight(QFont::Normal);
    subtitleLayout_.setFont(font);
    subtitleHeight_ = layoutText(subtitleLayout_, available);
    const int target =
        std::max(54, static_cast<int>(std::ceil(titleHeight_ + subtitleHeight_ + 24)));
    if (height() != target)
        setFixedHeight(target);
    update();
}
void LibraryBlockButton::resizeEvent(QResizeEvent* event) {
    QPushButton::resizeEvent(event);
    if (event->size().width() != event->oldSize().width())
        updateTextLayout(std::max(132, event->size().width()));
}
void LibraryBlockButton::changeEvent(QEvent* event) {
    QPushButton::changeEvent(event);
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
        updateTextLayout(std::max(132, width()));
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
    const qreal centerY = card.center().y();
    paintStudioIcon(painter, blockIcon(descriptor_),
                    compact_ ? card.adjusted(9, 9, -9, -9)
                             : QRectF(card.left() + (narrow_ ? 10 : 24), centerY - 11, 22, 22),
                    Theme::mint());
    if (!compact_) {
        font.setPixelSize(12);
        painter.setFont(font);
        painter.setPen(QColor(122, 137, 156));
        if (!narrow_)
            paintStudioIcon(painter, StudioIcon::Grip, QRectF(card.left() + 6, centerY - 8, 13, 16),
                            Theme::muted());
        const qreal textY = (height() - titleHeight_ - subtitleHeight_ - 4) / 2;
        painter.setPen(QColor(228, 232, 238));
        titleLayout_.draw(&painter, QPointF(card.left() + labelLeft_, textY));
        painter.setPen(Theme::muted());
        subtitleLayout_.draw(&painter, QPointF(card.left() + labelLeft_, textY + titleHeight_ + 4));
        font.setPixelSize(16);
        painter.setFont(font);
        painter.setPen(QColor(228, 232, 238));
        paintStudioIcon(painter, StudioIcon::Plus, QRectF(card.right() - 22, centerY - 7, 14, 14),
                        Theme::muted());
        if (property("favorite").toBool())
            paintStudioIcon(painter, StudioIcon::Star,
                            QRectF(card.right() - 19, card.top() + 5, 10, 10), Theme::mint());
    }
    painter.setBrush(Theme::headerBackground());
    const qreal portY = centerY;
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
