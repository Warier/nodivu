#pragma once
#include <QTextEdit>

namespace nodivu {
// Native, plain-text Studio notice shared by transport and capture controls.
class InfoMessage final : public QTextEdit {
    Q_OBJECT
  public:
    InfoMessage(bool compact, QWidget* parent = nullptr);
    void setMessage(const QString& text);

  signals:
    void geometryChanged();

  private:
    void updateHeight(qreal documentHeight);
    bool compact_;
};
} // namespace nodivu
