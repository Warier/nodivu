#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace nodivu {
struct JsonResult {
    QJsonObject object;
    QString error;
    bool ok() const {
        return error.isEmpty();
    }
};
JsonResult checkedJson(const QByteArray& bytes, qsizetype maximumBytes,
                       bool integerNumbers = false);
QByteArray canonicalJson(const QJsonValue& value);
} // namespace nodivu
