#pragma once
#include <QComboBox>
#include <QJsonArray>

namespace nodivu {
class DeviceSelector final : public QComboBox {
    Q_OBJECT
  public:
    explicit DeviceSelector(QWidget* parent = nullptr);
    void synchronize(const QJsonArray& devices, const QString& flow, const QString& endpointId);

  protected:
    void paintEvent(QPaintEvent* event) override;
};
} // namespace nodivu
