#pragma once

#include "features/workspace/session_controller.hpp"
#include <QFrame>
#include <QHash>
#include <QPointer>
class QLabel;
class QScrollArea;
class QVBoxLayout;
class QPushButton;
class QSlider;
class QComboBox;

namespace nodivu {
class PlayerDetails;
class WorkerControls;
class InspectorPanel final : public QFrame {
    Q_OBJECT
  public:
    explicit InspectorPanel(SessionController& session, QWidget* parent = nullptr);
    void inspect(const QString& id);
    void focusField(const QString& key);
    const QString& selectedId() const {
        return selectedId_;
    }
    void dockToCorner();
    void setSavedPath(const QString& path);

  signals:
    void reloadRequested(const QString& id);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void constrainToParent();
    void rebuild();
    void synchronize();
    void mountControl(QVBoxLayout* layout, const ControlDescriptor& control);
    void mountPlayer(QVBoxLayout* layout, const NodeDescriptor& descriptor);
    void mountCapture(QVBoxLayout* layout);
    SessionController& session_;
    QString selectedId_;
    QFrame* handle_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* subtitle_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QWidget* content_ = nullptr;
    QHash<QString, QPointer<QWidget>> fields_;
    QHash<QString, QPointer<QLabel>> values_;
    QPointer<WorkerControls> worker_;
    QPointer<PlayerDetails> playerDetails_;
    QPointer<QPushButton> playButton_;
    QPointer<QPushButton> reloadButton_;
    QPointer<QSlider> seek_;
    QPoint dragOffset_;
    bool dragging_ = false;
    bool collapsed_ = false;
    bool adjustingGeometry_ = false;
    QString savedPath_;
};
} // namespace nodivu
