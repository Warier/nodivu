#include "ui/controls/player_details.hpp"
#include "ui/controls/info_message.hpp"
#include <QLabel>
#include <QResizeEvent>
#include <QVBoxLayout>

namespace nodivu {
PlayerDetails::PlayerDetails(bool compact, bool transport, QWidget* parent)
    : QWidget(parent), compact_(compact) {
    setObjectName(QStringLiteral("playerDetails"));
    setStyleSheet(QStringLiteral("QWidget#playerDetails { background: transparent; }"
                                 "QLabel#playerTime { color: #dbe3ef; font-size: %1px; }"
                                 "QLabel#playerStatus { color: #c5cdd8; font-size: 10px; }")
                      .arg(compact ? 10 : 12));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    time_ = new QLabel(QStringLiteral("0:00 / 0:00"), this);
    time_->setObjectName(QStringLiteral("playerTime"));
    time_->setAlignment(Qt::AlignCenter);
    time_->setContentsMargins(0, compact ? 3 : 4, 0, compact ? 3 : 10);
    time_->setVisible(transport);
    layout->addWidget(time_);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("playerStatus"));
    status_->setTextFormat(Qt::PlainText);
    status_->setContentsMargins(0, 8, 0, 0);
    if (compact)
        status_->setFixedHeight(23);
    else
        status_->setWordWrap(true);
    status_->setMinimumWidth(0);
    layout->addWidget(status_);
    layout->addSpacing(compact ? 7 : 12);
    reason_ = new InfoMessage(compact, this);
    reason_->setObjectName(QStringLiteral("playerReason"));
    reason_->setAccessibleName(QStringLiteral("Informação sobre a reprodução"));
    connect(reason_, &InfoMessage::geometryChanged, this, &PlayerDetails::geometryChanged);
    layout->addWidget(reason_);
    layout->addSpacing(compact ? 7 : 6);
}
void PlayerDetails::synchronize(const PlaybackState& state, bool updatePosition) {
    statusText_ = state.name + QStringLiteral(" · ") + state.label;
    status_->setToolTip(state.file.isEmpty() ? statusText_
                                             : state.file + QLatin1Char('\n') + state.label);
    status_->setAccessibleName(statusText_);
    const auto message = state.reason.isEmpty() ? state.warning : state.reason;
    reason_->setMessage(message);
    reason_->setVisible(!message.isEmpty());
    if (updatePosition)
        previewPosition(state.positionMs, state.durationMs);
    updateTextLayout();
}
void PlayerDetails::previewPosition(int positionMs, int durationMs) {
    time_->setText(playbackTime(positionMs) + QStringLiteral(" / ") + playbackTime(durationMs));
}
void PlayerDetails::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateTextLayout();
}
void PlayerDetails::updateTextLayout() {
    status_->setText(compact_
                         ? status_->fontMetrics().elidedText(statusText_, Qt::ElideRight, width())
                         : statusText_);
}
} // namespace nodivu
