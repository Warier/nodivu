#include "features/graph/empty_graph_widget.hpp"
#include "features/graph/graph_view.hpp"

#include "ui/theme/studio_icons.hpp"
#include <QEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace nodivu {
EmptyGraphWidget::EmptyGraphWidget(QWidget* viewport) : QWidget(viewport) {
    setObjectName(QStringLiteral("emptyGraph"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    scroll_ = new QScrollArea(this);
    scroll_->setObjectName(QStringLiteral("welcomeScroll"));
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setStyleSheet(QStringLiteral(
        "QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }"));
    body_ = new QWidget(scroll_);
    scroll_->setWidget(body_);
    outer->addWidget(scroll_);
    auto* layout = new QVBoxLayout(body_);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);
    auto* symbol = new QLabel(this);
    symbol->setPixmap(studioIcon(StudioIcon::Wave, QColor(112, 217, 189)).pixmap(48, 48));
    symbol->setAlignment(Qt::AlignCenter);
    symbol->setStyleSheet(QStringLiteral("font-size: 64px; color: #536276;"));
    layout->addWidget(symbol);
    auto* title = new QLabel(QStringLiteral("Seu áudio, conectado."), this);
    title->setAlignment(Qt::AlignCenter);
    title->setWordWrap(true);
    title->setStyleSheet(QStringLiteral("font-size: 28px; font-weight: 500;"));
    layout->addWidget(title);
    auto* description =
        new QLabel(QStringLiteral("Escolha o que deseja usar. Depois, selecione a fonte e o "
                                  "destino\npara conferir seu caminho de áudio."),
                   this);
    description->setAlignment(Qt::AlignCenter);
    description->setWordWrap(true);
    description->setStyleSheet(QStringLiteral("font-size: 14px; color: #8b95a7;"));
    layout->addWidget(description);
    layout->addSpacing(12);
    start_ = new QPushButton(QStringLiteral("Usar microfone"), this);
    start_->setObjectName(QStringLiteral("quickStart"));
    start_->setAccessibleName(QStringLiteral("Adicionar entrada e saída"));
    start_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #65debc; border-color: #65debc; color: #123a30; "
        "font-weight: 650; padding: 10px 16px; }"
        "QPushButton:hover:enabled { background: #81e6ca; }"
        "QPushButton:disabled { background: #30443f; border-color: #43544e; color: #93a69f; }"));
    start_->setIcon(studioIcon(StudioIcon::Microphone, QColor(18, 58, 48)));
    start_->setEnabled(false);
    layout->addWidget(start_, 0, Qt::AlignHCenter);
    connect(start_, &QPushButton::clicked, this, &EmptyGraphWidget::startRequested);
    apps_ = new QPushButton(QStringLiteral("Capturar aplicativo"), this);
    apps_->setObjectName(QStringLiteral("startApplication"));
    apps_->setIcon(studioIcon(StudioIcon::Apps));
    file_ = new QPushButton(QStringLiteral("Reproduzir arquivo"), this);
    file_->setObjectName(QStringLiteral("startFile"));
    file_->setIcon(studioIcon(StudioIcon::File));
    layout->addWidget(apps_, 0, Qt::AlignHCenter);
    layout->addWidget(file_, 0, Qt::AlignHCenter);
    for (auto* button : {start_, apps_, file_}) {
        button->setMinimumWidth(230);
        button->installEventFilter(this);
    }
    connect(apps_, &QPushButton::clicked, this,
            [this] { emit sourceRequested(QStringLiteral("plugin"), appPlugin_); });
    connect(file_, &QPushButton::clicked, this,
            [this] { emit sourceRequested(QStringLiteral("plugin"), filePlugin_); });
    setReady(false);
    viewport->installEventFilter(this);
    centerInViewport();
}

void EmptyGraphWidget::setReady(bool ready) {
    ready_ = ready;
    start_->setEnabled(ready);
    apps_->setEnabled(ready && !appPlugin_.isEmpty());
    file_->setEnabled(ready && !filePlugin_.isEmpty());
    apps_->setToolTip(
        appPlugin_.isEmpty()
            ? QStringLiteral("Instale ou ative o plugin de captura de aplicativos em Plugins.")
            : QStringLiteral(
                  "Preparar captura de aplicativo e saída; escolha o aplicativo depois."));
    file_->setToolTip(filePlugin_.isEmpty()
                          ? QStringLiteral("Instale ou ative um player em Plugins.")
                          : QStringLiteral("Preparar player e saída; escolha o arquivo depois."));
}

void EmptyGraphWidget::setCatalog(const NodeCatalog& catalog) {
    appPlugin_.clear();
    filePlugin_.clear();
    for (const auto& d : catalog.entries()) {
        if (d.captureSource && d.inputs == 0 && appPlugin_.isEmpty())
            appPlugin_ = d.pluginId;
        if (d.filePlayer && d.inputs == 0 && filePlugin_.isEmpty())
            filePlugin_ = d.pluginId;
    }
    setReady(ready_);
}

bool EmptyGraphWidget::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::FocusIn &&
        (watched == start_ || watched == apps_ || watched == file_)) {
        auto* button = static_cast<QWidget*>(watched);
        QTimer::singleShot(0, this, [this, button] { scroll_->ensureWidgetVisible(button, 8, 8); });
    }
    if (event->type() == QEvent::Resize || event->type() == QEvent::Show ||
        event->type() == QEvent::Hide || event->type() == QEvent::Move) {
        // Scrollbars and the workspace layout can still resize the viewport
        // during this event. Center after those geometry changes have settled.
        QTimer::singleShot(0, this, &EmptyGraphWidget::centerInViewport);
    }
    return QWidget::eventFilter(watched, event);
}

void EmptyGraphWidget::watchOverlay(QWidget* overlay) {
    overlay->installEventFilter(this);
}

void EmptyGraphWidget::centerInViewport() {
    const auto* view = qobject_cast<GraphView*>(parentWidget()->parentWidget());
    const auto available = view ? view->availableViewportRect() : parentWidget()->rect();
    setFixedWidth(std::min(480, std::max(1, available.width() - 32)));
    const int preferred = std::max(body_->minimumSizeHint().height(),
                                   body_->layout()->totalHeightForWidth(std::max(1, width() - 16)));
    setFixedHeight(std::min(std::max(1, available.height() - 16), preferred));
    const auto offset = available.center() - rect().center();
    move(offset.x(), std::max(available.top(), offset.y()));
}
} // namespace nodivu
