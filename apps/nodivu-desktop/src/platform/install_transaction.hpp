#pragma once
#include <QString>
#include <functional>

namespace nodivu {
struct InstallResult {
    QString error;
    QString backup;
    bool migratedElectron = false;
    bool ok() const {
        return error.isEmpty();
    }
};
// The payload is already unpacked by NSIS. No project/profile data is deleted.
// Only sibling directories created for this target participate in a transaction.
class InstallTransaction {
  public:
    using Verifier = std::function<QString(const QString& installedDirectory)>;
    explicit InstallTransaction(const QString& target);
    InstallResult apply(const QString& payload, const Verifier& verify);
    QString recover();
    QString journalPath() const {
        return journal_;
    }

  private:
    QString checkTargetParent() const;
    QString recoverLocked();
    QString target_;
    QString parent_;
    QString identity_;
    QString journal_;
};
} // namespace nodivu
