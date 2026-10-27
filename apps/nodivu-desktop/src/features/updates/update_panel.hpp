#pragma once
#include "features/updates/update_service.hpp"
#include <QThreadPool>
#include <QWidget>
#include <memory>
class QLabel;
class QPushButton;
class QProgressBar;
namespace nodivu {
class UpdatePanel final : public QWidget {
    Q_OBJECT
  public:
    UpdatePanel(const QString& profile, const QString& source, QWidget* parent = nullptr);
    ~UpdatePanel() override;
    void setInstallBlockedReason(const QString& reason);
  signals:
    void installRequested(const QString& installer, const QString& sha256);

  private:
    void run(bool download);
    void updateAction();
    std::shared_ptr<UpdateService> service_;
    std::shared_ptr<TransferProgress> progress_;
    UpdateOffer offer_;
    QThreadPool pool_;
    QLabel* message_ = nullptr;
    QPushButton* action_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QString installBlockedReason_;
    bool busy_ = false;
};
} // namespace nodivu
