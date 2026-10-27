#pragma once
#include <QDialog>
namespace nodivu {
class UpdateWindow final : public QDialog {
    Q_OBJECT
  public:
    UpdateWindow(const QString& profile, const QString& source, QWidget* parent = nullptr);
  signals:
    void installRequested(const QString& installer, const QString& sha256);
};
} // namespace nodivu
