#pragma once
#include "contracts/node_descriptor.hpp"
#include "contracts/playback_state.hpp"
#include <QWidget>

class QVBoxLayout;
class QLabel;
class QProgressBar;
class QPushButton;
class QSlider;
namespace nodivu {
class PlayerDetails;
class WorkerControls;
class NodeContent final : public QWidget {
    Q_OBJECT
  public:
    NodeContent(const QString& nodeId, const NodeDescriptor& descriptor);
    void synchronize(const QJsonObject& block, const QJsonArray& devices, bool enabled, double peak,
                     const QJsonObject& runtime = {});
    void setPlayback(const PlaybackState& state, bool enabled);
    void focusPrimaryControl();
    void setSignalStatus(const QString& text);
    void addCaptureWidget(QWidget* widget);
    void resizeToContents();
  signals:
    void fieldEdited(const QString& field, const QJsonValue& value);
    void actionRequested(const QString& action);
    void gestureRequested(quint32 parameter, bool pressed);
    void geometryChanged();
    void interactionStarted();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    struct Field {
        ControlDescriptor descriptor;
        QWidget* control = nullptr;
        QLabel* value = nullptr;
    };
    void mountControl(const ControlDescriptor& descriptor);
    NodeDescriptor descriptor_;
    QVBoxLayout* layout_ = nullptr;
    QVector<Field> fields_;
    QProgressBar* meter_ = nullptr;
    QLabel* level_ = nullptr;
    PlayerDetails* playerDetails_ = nullptr;
    QPushButton* choose_ = nullptr;
    QPushButton* reload_ = nullptr;
    QPushButton* play_ = nullptr;
    QSlider* seek_ = nullptr;
    WorkerControls* worker_ = nullptr;
    PlaybackState playback_;
};
} // namespace nodivu
