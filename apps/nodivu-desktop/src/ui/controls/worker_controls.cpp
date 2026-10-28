#include "ui/controls/worker_controls.hpp"
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace nodivu {
WorkerControls::WorkerControls(bool compact, QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("workerControls"));
    setStyleSheet(
        QStringLiteral(
            "QLabel { color: #8b95a7; font-size: 11px; background: transparent; }"
            "QPushButton { color: #dbe3ef; background: #262e3b; border: 1px solid #384353; "
            "border-radius: 7px; font-size: %1px; padding: %2px; }"
            "QPushButton:enabled:hover { background: #354153; border-color: #65748a; }"
            "QPushButton:focus { border-color: #83ddc5; }"
            "QPushButton:disabled { color: #626b78; background: #202733; border-color: #2b3543; }")
            .arg(compact ? 10 : 11)
            .arg(compact ? 6 : 8));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(8);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("workerStatus"));
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    layout->addWidget(status_);
    restart_ = new QPushButton(QStringLiteral("Reiniciar worker"), this);
    restart_->setObjectName(QStringLiteral("workerRestart"));
    restart_->setCursor(Qt::ArrowCursor);
    layout->addWidget(restart_);
    connect(restart_, &QPushButton::clicked, this, &WorkerControls::restartRequested);
    synchronize({}, false);
}
void WorkerControls::synchronize(const QJsonObject& runtime, bool enabled) {
    const QStringList labels{QStringLiteral("Aguardando áudio"),
                             QStringLiteral("Preparando Python…"), QStringLiteral("Processando"),
                             QStringLiteral("Falha")};
    auto text = labels.value(runtime.value(QStringLiteral("status")).toInt(),
                             QStringLiteral("Indisponível")) +
                QStringLiteral(" · +20 ms · %1 blocos sem resposta")
                    .arg(runtime.value(QStringLiteral("missing_blocks")).toInt());
    const auto error = runtime.value(QStringLiteral("error")).toString();
    if (!error.isEmpty())
        text += QStringLiteral(" · ") + error;
    if (status_->text() != text) {
        status_->setText(text);
        emit geometryChanged();
    }
    restart_->setEnabled(enabled);
}
} // namespace nodivu
