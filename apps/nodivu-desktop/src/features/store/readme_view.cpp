#include "features/store/readme_view.hpp"
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QTextDocument>

namespace nodivu {
ReadmeView::ReadmeView(QWidget* parent) : QTextBrowser(parent) {
    setOpenLinks(false);
    setOpenExternalLinks(false);
    connect(this, &QTextBrowser::anchorClicked, this, [](const QUrl& url) {
        if (url.scheme() == QStringLiteral("https") && url.userInfo().isEmpty())
            QDesktopServices::openUrl(url);
    });
}
void ReadmeView::showContent(const ReadmeContent& content) {
    directory_ = content.directory;
    images_ = content.images;
    document()->clear();
    document()->setDefaultStyleSheet(QStringLiteral(
        "body { color: #dbe3ef; } a { color: #70d9bd; } table { border-collapse: collapse; } td, "
        "th { padding: 6px; border: 1px solid #3d4b5a; } pre { background-color: #121924; }"));
    auto features = QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub);
    features |= QTextDocument::MarkdownNoHTML;
    document()->setMarkdown(content.error.isEmpty() ? content.markdown : content.error, features);
}
QVariant ReadmeView::loadResource(int type, const QUrl& name) {
    if (type != QTextDocument::ImageResource || !name.scheme().isEmpty() ||
        !name.host().isEmpty() || !name.query().isEmpty() || !name.fragment().isEmpty())
        return {};
    const auto relative = name.path();
    if (!PackageService::safeRelativePath(relative))
        return {};
    QString file;
    if (images_.contains(relative)) {
        file = images_.value(relative);
    } else if (!directory_.isEmpty()) {
        const auto root = QFileInfo(directory_).canonicalFilePath();
        const auto candidate = QFileInfo(QDir(directory_).filePath(relative)).canonicalFilePath();
        if (root.isEmpty() || !candidate.startsWith(root + QLatin1Char('/'), Qt::CaseInsensitive))
            return {};
        file = candidate;
    } else {
        return {};
    }
    const auto extension = QFileInfo(relative).suffix().toLower();
    if (extension != QStringLiteral("png") && extension != QStringLiteral("jpg") &&
        extension != QStringLiteral("jpeg") && extension != QStringLiteral("webp") &&
        extension != QStringLiteral("gif"))
        return {};
    QFile input(file);
    const QFileInfo info(file);
    if (info.isSymbolicLink() || info.isJunction() || !input.open(QIODevice::ReadOnly) ||
        input.size() > 2 * 1024 * 1024)
        return {};
    QImageReader reader(&input);
    reader.setAutoDetectImageFormat(true);
    const auto size = reader.size();
    if (!size.isValid() || size.width() > 4096 || size.height() > 4096 ||
        static_cast<qint64>(size.width()) * size.height() > 8 * 1024 * 1024)
        return {};
    return reader.read();
}
} // namespace nodivu
