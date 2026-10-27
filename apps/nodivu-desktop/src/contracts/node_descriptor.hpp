#pragma once

#include <QColor>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

namespace nodivu {
enum class ControlKind { Number, Slider, Toggle, Select, Device, Momentary };

struct ControlDescriptor {
    QString key;
    QString label;
    QString unit;
    QString group;
    QString page;
    QString help;
    QString flow;
    ControlKind kind = ControlKind::Number;
    double minimum = 0;
    double maximum = 1;
    double step = 0.01;
    bool readonly = false;
    bool advanced = false;
    bool quick = false;
    QJsonArray options;
};

struct NodeDescriptor {
    QString kind;
    QString pluginId;
    QString title;
    QString subtitle;
    QString description;
    QString layout = QStringLiteral("stack");
    QColor accent = QColor(112, 217, 189);
    int inputs = 1;
    int outputs = 1;
    QStringList inputLabels;
    bool filePlayer = false;
    bool fileTransport = false;
    bool captureSource = false;
    bool worker = false;
    bool missing = false;
    QVector<ControlDescriptor> controls;
    QString catalogKey() const;
    QString icon() const;
    bool needsInspector() const;
};

class NodeCatalog {
  public:
    NodeCatalog();
    void setPlugins(const QJsonArray& plugins);
    QVector<NodeDescriptor> entries() const;
    NodeDescriptor describe(const QJsonObject& node) const;

  private:
    QHash<QString, NodeDescriptor> internal_;
    QHash<QString, NodeDescriptor> plugins_;
    QStringList pluginOrder_;
};

QJsonValue fieldValue(const QJsonObject& block, const QString& key);
void setFieldValue(QJsonObject& block, const QString& key, const QJsonValue& value);
} // namespace nodivu
