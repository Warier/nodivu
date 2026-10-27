#include "features/graph/empty_graph_widget.hpp"

#include <QEvent>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace nodivu {
EmptyGraphWidget::EmptyGraphWidget(QWidget* viewport) : QWidget(viewport) {
    setObjectName(QStringLiteral("emptyGraph"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);
    auto* symbol = new QLabel(QStringLiteral("◌"), this);
    symbol->setAlignment(Qt::AlignCenter);
    symbol->setStyleSheet(QStringLiteral("font-size: 64px; color: #536276;"));
    layout->addWidget(symbol);
    auto* title = new QLabel(QStringLiteral("Seu áudio, conectado."), this);
    title->setAlignment(Qt::AlignCenter);
    title->setWordWrap(true);
    title->setStyleSheet(QStringLiteral("font-size: 28px; font-weight: 500;"));
    layout->addWidget(title);
    auto* description = new QLabel(
        QStringLiteral("Comece adicionando uma entrada e uma saída\npela biblioteca ao lado."),
        this);
    description->setAlignment(Qt::AlignCenter);
    description->setWordWrap(true);
    description->setStyleSheet(QStringLiteral("font-size: 14px; color: #8b95a7;"));
    layout->addWidget(description);
    layout->addSpacing(12);
    start_ = new QPushButton(QStringLiteral("+ Adicionar os dois blocos"), this);
    start_->setObjectName(QStringLiteral("quickStart"));
    start_->setAccessibleName(QStringLiteral("Adicionar entrada e saída"));
    start_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #65debc; border-color: #65debc; color: #123a30; "
        "font-weight: 650; padding: 10px 16px; }"
        "QPushButton:hover:enabled { background: #81e6ca; }"
        "QPushButton:disabled { background: #30443f; border-color: #43544e; color: #93a69f; }"));
    start_->setEnabled(false);
    layout->addWidget(start_, 0, Qt::AlignHCenter);
    connect(start_, &QPushButton::clicked, this, &EmptyGraphWidget::startRequested);
    viewport->installEventFilter(this);
    centerInViewport();
}

void EmptyGraphWidget::setReady(bool ready) {
    start_->setEnabled(ready);
}

bool EmptyGraphWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
        // Scrollbars and the workspace layout can still resize the viewport
        // during this event. Center after those geometry changes have settled.
        QTimer::singleShot(0, this, &EmptyGraphWidget::centerInViewport);
    }
    return QWidget::eventFilter(watched, event);
}

void EmptyGraphWidget::centerInViewport() {
    const auto available = parentWidget()->size();
    setFixedWidth(std::min(480, std::max(1, available.width() - 32)));
    adjustSize();
    const auto offset = parentWidget()->rect().center() - rect().center();
    move(offset.x(), std::max(0, offset.y()));
}
} // namespace nodivu
