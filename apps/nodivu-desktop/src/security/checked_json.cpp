#include "security/checked_json.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QStringDecoder>
#include <algorithm>
#include <cmath>

namespace nodivu {
namespace {
// Preflight preserves the original bytes and rejects duplicate keys before Qt's
// object representation could discard them. Qt still performs final JSON parsing.
class Scanner {
  public:
    Scanner(const QByteArray& bytes, bool integers) : bytes_(bytes), integers_(integers) {}
    bool valid() {
        whitespace();
        if (!value(0))
            return false;
        whitespace();
        return position_ == bytes_.size();
    }

  private:
    void whitespace() {
        while (position_ < bytes_.size() &&
               (bytes_[position_] == ' ' || bytes_[position_] == '\r' ||
                bytes_[position_] == '\n' || bytes_[position_] == '\t'))
            ++position_;
    }
    bool consume(char expected) {
        whitespace();
        if (position_ >= bytes_.size() || bytes_[position_] != expected)
            return false;
        ++position_;
        return true;
    }
    bool string(QString* decoded = nullptr) {
        whitespace();
        if (position_ >= bytes_.size() || bytes_[position_] != '"')
            return false;
        const auto start = position_++;
        while (position_ < bytes_.size()) {
            const char character = bytes_[position_++];
            if (character == '\\') {
                if (position_ >= bytes_.size())
                    return false;
                ++position_;
            } else if (character == '"') {
                const auto document = QJsonDocument::fromJson(
                    QByteArray("[") + bytes_.mid(start, position_ - start) + QByteArray("]"));
                if (!document.isArray() || document.array().size() != 1 ||
                    !document.array().first().isString())
                    return false;
                const auto text = document.array().first().toString();
                for (qsizetype i = 0; i < text.size(); ++i) {
                    if (text[i].isHighSurrogate()) {
                        ++i;
                        if (i >= text.size() || !text[i].isLowSurrogate())
                            return false;
                    } else if (text[i].isLowSurrogate()) {
                        return false;
                    }
                }
                if (decoded)
                    *decoded = text;
                return true;
            }
        }
        return false;
    }
    bool value(int depth) {
        whitespace();
        if (depth > 64 || position_ >= bytes_.size())
            return false;
        const char character = bytes_[position_];
        if (character == '"')
            return string();
        if (character == '{') {
            ++position_;
            QSet<QString> keys;
            whitespace();
            if (consume('}'))
                return true;
            do {
                QString key;
                if (!string(&key) || keys.contains(key) || !consume(':'))
                    return false;
                keys.insert(key);
                if (!value(depth + 1))
                    return false;
                whitespace();
                if (consume('}'))
                    return true;
            } while (consume(','));
            return false;
        }
        if (character == '[') {
            ++position_;
            whitespace();
            if (consume(']'))
                return true;
            do {
                if (!value(depth + 1))
                    return false;
                whitespace();
                if (consume(']'))
                    return true;
            } while (consume(','));
            return false;
        }
        for (const auto& literal : {QByteArray("true"), QByteArray("false"), QByteArray("null")})
            if (bytes_.mid(position_, literal.size()) == literal) {
                position_ += literal.size();
                return true;
            }
        const auto start = position_;
        while (position_ < bytes_.size()) {
            const auto digit = bytes_[position_];
            if ((digit >= '0' && digit <= '9') || digit == '-' || digit == '+' || digit == '.' ||
                digit == 'e' || digit == 'E')
                ++position_;
            else
                break;
        }
        if (start == position_)
            return false;
        const auto token = bytes_.mid(start, position_ - start);
        const auto document = QJsonDocument::fromJson(QByteArray("[") + token + QByteArray("]"));
        if (!document.isArray() || document.array().size() != 1 ||
            !document.array().first().isDouble())
            return false;
        const double number = document.array().first().toDouble();
        return std::isfinite(number) &&
               (!integers_ ||
                (std::abs(number) <= 9007199254740991.0 && std::floor(number) == number &&
                 !token.contains('.') && !token.contains('e') && !token.contains('E')));
    }
    const QByteArray& bytes_;
    qsizetype position_ = 0;
    bool integers_;
};
QByteArray canonicalString(const QString& string) {
    auto encoded = string.toUtf8();
    encoded.replace("\\", "\\\\");
    encoded.replace("\"", "\\\"");
    return QByteArray("\"") + encoded + QByteArray("\"");
}
} // namespace

JsonResult checkedJson(const QByteArray& bytes, qsizetype maximumBytes, bool integerNumbers) {
    if (bytes.isEmpty() || bytes.size() > maximumBytes)
        return {{}, QStringLiteral("JSON vazio ou acima do limite.")};
    QStringDecoder utf8(QStringDecoder::Utf8);
    const QString decoded = utf8(bytes);
    Q_UNUSED(decoded);
    if (utf8.hasError() || !Scanner(bytes, integerNumbers).valid())
        return {{},
                QStringLiteral(
                    "JSON inválido, duplicado, profundo demais ou com tipos incompatíveis.")};
    const auto document = QJsonDocument::fromJson(bytes);
    if (!document.isObject())
        return {{}, QStringLiteral("Era esperado um objeto JSON.")};
    return {document.object(), {}};
}
QByteArray canonicalJson(const QJsonValue& value) {
    if (value.isString())
        return canonicalString(value.toString());
    if (value.isBool())
        return value.toBool() ? QByteArray("true") : QByteArray("false");
    if (value.isNull())
        return QByteArray("null");
    if (value.isDouble())
        return QByteArray::number(value.toInteger());
    QByteArray result;
    if (value.isArray()) {
        result.append('[');
        bool first = true;
        for (const auto& item : value.toArray()) {
            if (!first)
                result.append(',');
            first = false;
            result.append(canonicalJson(item));
        }
        result.append(']');
    } else if (value.isObject()) {
        result.append('{');
        const auto object = value.toObject();
        auto keys = object.keys();
        std::sort(keys.begin(), keys.end());
        bool first = true;
        for (const auto& key : keys) {
            if (!first)
                result.append(',');
            first = false;
            result.append(canonicalString(key));
            result.append(':');
            result.append(canonicalJson(object.value(key)));
        }
        result.append('}');
    }
    return result;
}
} // namespace nodivu
