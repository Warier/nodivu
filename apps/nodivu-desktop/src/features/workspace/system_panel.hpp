#pragma once
#include "features/workspace/session_controller.hpp"
#include "platform/app_paths.hpp"
#include <QFrame>
class QLabel;
class QComboBox;
class QPushButton;
class QProgressBar;
class QScrollArea;
class QVBoxLayout;
class QTabBar;
class QStackedWidget;
namespace nodivu {
class ProjectController;
class UpdatePanel;
class SystemPanel final : public QFrame {
    Q_OBJECT
  public:
    SystemPanel(SessionController& session, ProjectController& projects, AppPaths paths,
                QWidget* parent);
    void openMonitor();
    void openProject();
    void openApplication();
    void synchronize();
  signals:
    void shown();
    void storeRequested();
    void discardRecoveryRequested();
    void installRequested(const QString& file, const QString& hash);
    void error(const QString& message);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

  private:
    void fitToParent();
    void synchronizeMonitor();
    void synchronizeConnections();
    void synchronizeCable();
    SessionController& session_;
    ProjectController& projects_;
    AppPaths paths_;
    QScrollArea* scroll_ = nullptr;
    QTabBar* tabs_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QWidget* monitorSection_ = nullptr;
    QComboBox* device_ = nullptr;
    QPushButton* monitor_ = nullptr;
    QLabel* monitorStatus_ = nullptr;
    QLabel* monitorHint_ = nullptr;
    QLabel* cableStatus_ = nullptr;
    QLabel* cableRoute_ = nullptr;
    QLabel* signal_ = nullptr;
    QLabel* inputDb_ = nullptr;
    QLabel* outputDb_ = nullptr;
    QProgressBar* inputMeter_ = nullptr;
    QProgressBar* outputMeter_ = nullptr;
    QLabel* metrics_ = nullptr;
    QLabel* recovery_ = nullptr;
    QPushButton* restore_ = nullptr;
    QPushButton* discard_ = nullptr;
    QPushButton* plugins_ = nullptr;
    QVBoxLayout* connections_ = nullptr;
    QJsonObject graph_;
    QJsonArray monitorDevices_;
    QString monitorId_;
    UpdatePanel* updates_ = nullptr;
};
} // namespace nodivu
