#include "features/updates/update_window.hpp"
#include "features/updates/update_panel.hpp"
#include "ui/theme/studio_panel_style.hpp"
#include <QVBoxLayout>
namespace nodivu {
UpdateWindow::UpdateWindow(const QString& profile, const QString& source, QWidget* parent)
    : QDialog(parent) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QStringLiteral("Atualizações do Nodivu"));
    setStyleSheet(studioPanelStyle());
    resize(400, 280);
    auto* layout = new QVBoxLayout(this);
    auto* panel = new UpdatePanel(profile, source, this);
    layout->addWidget(panel);
    connect(panel, &UpdatePanel::installRequested, this, &UpdateWindow::installRequested);
}
} // namespace nodivu
