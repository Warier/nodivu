#pragma once
#include "features/store/store_service.hpp"
#include <QTextBrowser>

namespace nodivu {
class ReadmeView final : public QTextBrowser {
    Q_OBJECT
  public:
    explicit ReadmeView(QWidget* parent = nullptr);
    void showContent(const ReadmeContent& content);

  protected:
    QVariant loadResource(int type, const QUrl& name) override;

  private:
    QString directory_;
    QHash<QString, QString> images_;
};
} // namespace nodivu
