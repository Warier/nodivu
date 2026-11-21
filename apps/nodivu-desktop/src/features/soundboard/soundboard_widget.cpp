#include "features/soundboard/soundboard_widget.hpp"
#include "contracts/playback_state.hpp"
#include "features/hotkeys/hotkey_service.hpp"
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
namespace nodivu {
SoundboardWidget::SoundboardWidget(SessionController& session, QString id, bool compact,
                                   QWidget* parent)
    : QWidget(parent), session_(session), id_(std::move(id)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    auto* grid = new QGridLayout;
    grid->setSpacing(4);
    layout->addLayout(grid);
    const int count = session_.catalog().describe(session_.node(id_)).fileBankSlots;
    for (int slot = 0; slot < count; ++slot) {
        Row row;
        row.play = new QPushButton(QString::number((slot + 1) % 10), this);
        row.play->setObjectName(QStringLiteral("soundboardPlay%1").arg(slot));
        row.play->setAccessibleName(QStringLiteral("Tocar som %1").arg(slot + 1));
        row.play->setMinimumWidth(28);
        connect(row.play, &QPushButton::clicked, this,
                [this, slot] { command(slot, QStringLiteral("trigger")); });
        if (compact)
            grid->addWidget(row.play, slot / 5, slot % 5);
        else {
            row.choose = new QPushButton(QStringLiteral("Escolher áudio…"), this);
            row.choose->setObjectName(QStringLiteral("soundboardFile%1").arg(slot));
            row.stop = new QPushButton(QStringLiteral("Parar"), this);
            row.clear = new QPushButton(QStringLiteral("×"), this);
            row.clear->setToolTip(QStringLiteral("Remover arquivo desta posição"));
            row.status = new QLabel(this);
            row.status->setWordWrap(true);
            row.status->setTextFormat(Qt::PlainText);
            row.status->setStyleSheet(QStringLiteral("font-size:10px;color:#aab5c4"));
            grid->addWidget(row.play, slot * 2, 0);
            grid->addWidget(row.choose, slot * 2, 1);
            grid->addWidget(row.stop, slot * 2, 2);
            grid->addWidget(row.clear, slot * 2, 3);
            grid->addWidget(row.status, slot * 2 + 1, 0, 1, 4);
            connect(row.choose, &QPushButton::clicked, this, [this, slot] {
                const auto path = QFileDialog::getOpenFileName(
                    this, QStringLiteral("Som %1 · até 60 segundos / 32 MiB").arg(slot + 1), {},
                    QStringLiteral("Áudio (*.mp3 *.wav *.m4a)"));
                if (!path.isEmpty())
                    command(slot, QStringLiteral("load"), path);
            });
            connect(row.stop, &QPushButton::clicked, this,
                    [this, slot] { command(slot, QStringLiteral("stop")); });
            connect(row.clear, &QPushButton::clicked, this,
                    [this, slot] { command(slot, QStringLiteral("clear")); });
        }
        rows_.append(row);
    }
    if (!compact) {
        auto* preset = new QPushButton(QStringLiteral("Atalhos 1–0 (Ctrl+Shift)"), this);
        auto* customize = new QPushButton(QStringLiteral("Personalizar atalhos…"), this);
        layout->addWidget(preset);
        layout->addWidget(customize);
        connect(preset, &QPushButton::clicked, this, [this] {
            if (auto* service = window()->findChild<HotkeyService*>()) {
                const auto error = service->soundboardPreset(id_);
                if (!error.isEmpty())
                    QMessageBox::warning(this, QStringLiteral("Atalhos"), error);
                else
                    showHotkeys(*service, this);
            }
        });
        connect(customize, &QPushButton::clicked, this, [this] {
            if (auto* service = window()->findChild<HotkeyService*>())
                showHotkeys(*service, this);
        });
    }
    connect(&session_, &SessionController::changed, this, [this] { synchronize(); });
    connect(&session_, &SessionController::activityChanged, this, [this] { synchronize(); });
    synchronize();
}
void SoundboardWidget::command(int slot, const QString& action, const QString& path) {
    if (!session_.settled())
        return;
    if (action == QStringLiteral("trigger") &&
        !bankPlaybackState(session_.snapshot(), session_.node(id_), session_.devices(),
                           session_.online(), false, slot)
             .canPlay)
        return;
    QJsonObject args{{QStringLiteral("node_id"), id_},
                     {QStringLiteral("slot"), slot},
                     {QStringLiteral("action"), action}};
    if (action == QStringLiteral("load"))
        args.insert(QStringLiteral("path"), path);
    session_.command(QStringLiteral("plugin.bank"), args);
}
void SoundboardWidget::synchronize() {
    for (int slot = 0; slot < rows_.size(); ++slot) {
        const auto state =
            bankPlaybackState(session_.snapshot(), session_.node(id_), session_.devices(),
                              session_.online(), !session_.settled(), slot);
        auto& row = rows_[slot];
        row.play->setEnabled(state.canPlay);
        row.play->setToolTip(state.reason.isEmpty()
                                 ? state.name + QStringLiteral(" · tocar/reiniciar")
                                 : state.reason);
        row.play->setStyleSheet(state.playing ? QStringLiteral("background:#387869;color:white")
                                              : QString());
        if (row.choose) {
            row.choose->setEnabled(session_.settled());
            row.clear->setEnabled(session_.settled() && !state.file.isEmpty());
            row.stop->setEnabled(session_.settled() && !state.file.isEmpty());
            row.choose->setText(state.file.isEmpty()
                                    ? QStringLiteral("Escolher áudio…")
                                    : fontMetrics().elidedText(state.name, Qt::ElideMiddle, 120));
            row.choose->setToolTip(state.file);
            row.status->setText(!state.reason.isEmpty()
                                    ? state.reason
                                    : (state.label + QStringLiteral(" · ") +
                                       playbackTime(state.positionMs) + QStringLiteral(" / ") +
                                       playbackTime(state.durationMs)));
        }
    }
}
} // namespace nodivu
