#include "features/updates/update_service.hpp"
#include "features/plugins/package_service.hpp"
#include "security/checked_json.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QVersionNumber>

namespace nodivu {
namespace {
struct Version {
    QVersionNumber number;
    QString suffix;
    bool valid = false;
};
Version parseVersion(const QString& value) {
    static const QRegularExpression expression(
        QStringLiteral("^(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})(-(beta|"
                       "dev)\\.(0|[1-9][0-9]{0,5}))?$"));
    const auto match = expression.match(value);
    return {QVersionNumber::fromString(value), match.captured(4), match.hasMatch()};
}
bool newer(const QString& candidate, const QString& current) {
    const auto left = parseVersion(candidate);
    const auto right = parseVersion(current);
    const int comparison = QVersionNumber::compare(left.number, right.number);
    if (comparison != 0)
        return comparison > 0;
    if (left.suffix.isEmpty() != right.suffix.isEmpty())
        return left.suffix.isEmpty();
    if (left.suffix.section(QLatin1Char('.'), 0, 0) != right.suffix.section(QLatin1Char('.'), 0, 0))
        return left.suffix.startsWith(QStringLiteral("-beta")) &&
               right.suffix.startsWith(QStringLiteral("-dev"));
    return left.suffix.section(QLatin1Char('.'), 1).toInt() >
           right.suffix.section(QLatin1Char('.'), 1).toInt();
}
} // namespace
UpdateService::UpdateService(QString profile, QByteArray bootstrap, QString currentVersion,
                             QString channel, bool allowLoopbackForTests)
    // Application trust has its own namespace and bootstrap, never plugin keys.
    : repository_(QDir(profile).filePath(QStringLiteral("updates/trust")), std::move(bootstrap),
                  allowLoopbackForTests),
      profile_(std::move(profile)), currentVersion_(std::move(currentVersion)),
      channel_(std::move(channel)) {}
UpdateOffer UpdateService::parseOffer(const QByteArray& contents, const QString& currentVersion,
                                      const QString& channel) {
    UpdateOffer result;
    const auto parsed = checkedJson(contents, 16LL * 1024, true);
    if (!parsed.ok()) {
        result.error = parsed.error;
        return result;
    }
    const auto value = parsed.object;
    result.version = value.value(QStringLiteral("version")).toString();
    result.target = value.value(QStringLiteral("installer")).toString();
    result.notes = value.value(QStringLiteral("notes")).toString();
    result.bytes = value.value(QStringLiteral("bytes")).toInteger(-1);
    if (value.value(QStringLiteral("schema_version")).toInt() != 1 ||
        value.value(QStringLiteral("platform")).toString() != QStringLiteral("win32-x64") ||
        value.value(QStringLiteral("channel")).toString() != channel ||
        (channel != QStringLiteral("stable") && channel != QStringLiteral("beta")) ||
        !parseVersion(result.version).valid || !parseVersion(currentVersion).valid ||
        (channel == QStringLiteral("stable") && !parseVersion(result.version).suffix.isEmpty()) ||
        !PackageService::safeRelativePath(result.target) ||
        !result.target.endsWith(QStringLiteral("-setup.exe")) || result.bytes < 1 ||
        result.bytes > 256LL * 1024LL * 1024 || result.notes.size() > 8192) {
        result.error = QStringLiteral("Oferta de atualização incompatível ou inválida.");
        return result;
    }
    result.available = newer(result.version, currentVersion);
    return result;
}
UpdateOffer UpdateService::check(TransferProgress* progress) {
    offer_ = {};
    offer_.error = repository_.initialize();
    if (offer_.error.isEmpty())
        offer_.error = repository_.refresh(progress);
    if (!offer_.error.isEmpty())
        return offer_;
    const auto target = repository_.target(
        QStringLiteral("windows-x64/") + channel_ + QStringLiteral(".json"), 16LL * 1024, progress);
    if (!target.ok()) {
        offer_.error = target.error;
        return offer_;
    }
    offer_ = parseOffer(target.bytes, currentVersion_, channel_);
    return offer_;
}
UpdateOffer UpdateService::download(TransferProgress* progress) {
    if (!offer_.available || !offer_.error.isEmpty()) {
        offer_.error = QStringLiteral("Verifique uma atualização autenticada antes de baixar.");
        return offer_;
    }
    const auto target = repository_.target(offer_.target, offer_.bytes, progress, offer_.bytes);
    if (!target.ok()) {
        offer_.error = target.error;
        return offer_;
    }
    const auto directory = QDir(profile_).filePath(QStringLiteral("updates/downloads"));
    if (!QDir().mkpath(directory) || QFileInfo(directory).isJunction() ||
        QFileInfo(directory).isSymbolicLink()) {
        offer_.error = QStringLiteral("Diretório de atualização indisponível.");
        return offer_;
    }
    const auto destination = QDir(directory).filePath(QStringLiteral("Nodivu-") + offer_.version +
                                                      QStringLiteral("-setup.exe"));
    QFile input(target.file);
    QSaveFile output(destination);
    output.setDirectWriteFallback(false);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
        offer_.error = QStringLiteral("Não foi possível preparar o instalador verificado.");
        return offer_;
    }
    qint64 copied = 0;
    while (!input.atEnd()) {
        if (progress && progress->cancelled.load()) {
            offer_.error = QStringLiteral("Atualização cancelada.");
            return offer_;
        }
        const auto block = input.read(64LL * 1024);
        if (block.isEmpty() || copied > offer_.bytes - block.size() ||
            output.write(block) != block.size()) {
            offer_.error = QStringLiteral("Falha ao preparar o instalador.");
            return offer_;
        }
        copied += block.size();
        hash.addData(block);
    }
    const auto authenticatedHash = target.authenticated.value(QStringLiteral("hashes"))
                                       .toObject()
                                       .value(QStringLiteral("sha256"))
                                       .toString()
                                       .toLatin1()
                                       .toLower();
    if (copied != offer_.bytes || hash.result().toHex() != authenticatedHash || !output.commit()) {
        offer_.error = QStringLiteral("Instalador incompleto; versão atual preservada.");
        return offer_;
    }
    offer_.installer = destination;
    offer_.sha256 = QString::fromLatin1(hash.result().toHex());
    return offer_;
}
} // namespace nodivu
