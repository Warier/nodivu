#pragma once
#include <QLocalServer>
#include <QLockFile>
#include <functional>
#include <memory>

namespace nodivu {
class SingleInstance final : public QObject {
    Q_OBJECT
  public:
    explicit SingleInstance(const QString& profile, QObject* parent = nullptr);
    bool acquire();
    QString error() const {
        return error_;
    }
  signals:
    void activated();

  private:
    QString name_;
    QString error_;
    std::unique_ptr<QLockFile> lock_;
    QLocalServer server_;
};
} // namespace nodivu
