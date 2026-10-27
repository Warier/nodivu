#pragma once

#include "backend/backend_client.hpp"
#include "features/workspace/session_controller.hpp"
#include "platform/app_paths.hpp"

#include <QMainWindow>
#include <QSet>
class QLabel;
class QCloseEvent;
class QMenu;
class QAction;
class QPushButton;
class QDockWidget;

namespace nodivu {
class LibraryPanel;
class GraphScene;
class GraphView;
class EmptyGraphWidget;
class InspectorPanel;
class ProjectController;
class StoreController;
class SystemPanel;
class AudioStatusPanel;
class MainWindow final : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(const AppPaths& paths, QWidget* parent = nullptr);
    ~MainWindow() override;
    SessionController& session() {
        return session_;
    }
    QString installer() const {
        return installer_;
    }
    QString installerHash() const {
        return installerHash_;
    }
  signals:
    void openRequested();
    void saveRequested();
    void storeRequested();

  protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;

  private:
    void updateStatus();
    void updateZoomControls();
    void updateActions();
    void updateRecents();
    void discardRecovery();
    void configureVirtualOutput();
    void showSettings();
    void handleAudioAction();
    void nodeAction(const QString& id, const QString& action, const QPoint& position);
    BackendClient backend_;
    SessionController session_{backend_, this};
    AppPaths paths_;
    LibraryPanel* library_ = nullptr;
    GraphScene* scene_ = nullptr;
    GraphView* view_ = nullptr;
    EmptyGraphWidget* emptyGraph_ = nullptr;
    InspectorPanel* inspector_ = nullptr;
    AudioStatusPanel* audioStatus_ = nullptr;
    QLabel* revision_ = nullptr;
    QLabel* error_ = nullptr;
    QLabel* projectTitle_ = nullptr;
    QMenu* recents_ = nullptr;
    ProjectController* projects_ = nullptr;
    QWidget* recoveryBanner_ = nullptr;
    StoreController* store_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* mute_ = nullptr;
    QPushButton* zoomIn_ = nullptr;
    QPushButton* zoomOut_ = nullptr;
    QPushButton* zoomReset_ = nullptr;
    QPushButton* fit_ = nullptr;
    QAction* open_ = nullptr;
    QAction* save_ = nullptr;
    QAction* listen_ = nullptr;
    QAction* settings_ = nullptr;
    QAction* inspect_ = nullptr;
    QAction* organize_ = nullptr;
    QAction* routing_ = nullptr;
    QAction* refreshDevices_ = nullptr;
    QAction* retryDevices_ = nullptr;
    QAction* virtualOutput_ = nullptr;
    QAction* managePlugins_ = nullptr;
    QAction* muteAction_ = nullptr;
    QDockWidget* routingDock_ = nullptr;
    SystemPanel* system_ = nullptr;
    bool closePending_ = false;
    bool closing_ = false;
    bool shutdownComplete_ = false;
    bool virtualOutputPending_ = false;
    QString installer_;
    QString installerHash_;
    QString lastSnapshotError_;
    QSet<QString> droppedNodes_;
};
} // namespace nodivu
