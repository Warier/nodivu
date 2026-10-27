#pragma once
#include <QJsonArray>
#include <QJsonObject>
namespace nodivu {
enum class AudioAction { None, Output, Retry, Refresh, Reconnect, Unmute };
struct AudioState {
    QString title;
    QString message;
    QString label;
    AudioAction action = AudioAction::None;
    bool ready = false;
};
AudioState audioState(const QJsonObject& snapshot, const QJsonArray& devices, bool online,
                      const QString& deviceIssue = {});
} // namespace nodivu
