#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace nodivu {
// Presentation only: this never reads a file or changes engine state.
struct PlaybackState {
    QString file;
    QString name;
    QString label;
    QString reason;
    QString warning;
    int positionMs = 0;
    int durationMs = 0;
    bool playing = false;
    bool canPlay = false;
    bool needsLoad = false;
    bool operator==(const PlaybackState&) const = default;
};
PlaybackState playbackState(const QJsonObject& snapshot, const QJsonObject& node,
                            const QJsonArray& devices, bool online, bool busy,
                            const QString& savedPath = {});
QString playbackTime(int milliseconds);
} // namespace nodivu
