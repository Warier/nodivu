#pragma once
#include <QColor>
#include <QIcon>
#include <QRectF>
class QPainter;
namespace nodivu {
struct NodeDescriptor;
enum class StudioIcon {
    Microphone,
    Output,
    File,
    Apps,
    Gain,
    Mixer,
    Meter,
    Plugin,
    Wave,
    More,
    Close,
    Menu,
    Plus,
    Minus,
    Target,
    Grip,
    Play,
    Pause,
    Search,
    Check,
    Undo,
    Redo,
    Folder,
    Warning
};
StudioIcon blockIcon(const NodeDescriptor& descriptor);
void paintStudioIcon(QPainter& painter, StudioIcon icon, const QRectF& bounds, const QColor& color);
QIcon studioIcon(StudioIcon icon, const QColor& color = QColor(190, 204, 222));
} // namespace nodivu
