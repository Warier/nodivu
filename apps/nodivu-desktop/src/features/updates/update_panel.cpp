#include "features/updates/update_panel.hpp"
#include "ui/controls/action_availability.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QFutureWatcher>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrentRun>

namespace nodivu {
UpdatePanel::UpdatePanel(const QString& profile, const QString& source, QWidget* parent)
    : QWidget(parent) {
    pool_.setMaxThreadCount(1);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    const auto current = QCoreApplication::applicationVersion();
    auto* version =
        new QLabel(QStringLiteral("Versão %1 · testes")
                       .arg(current.isEmpty() ? QStringLiteral("desenvolvimento") : current),
                   this);
    version->setTextFormat(Qt::PlainText);
    layout->addWidget(version);
    message_ = new QLabel(this);
    message_->setObjectName(QStringLiteral("updateMessage"));
    message_->setWordWrap(true);
    message_->setTextFormat(Qt::PlainText);
    layout->addWidget(message_);
    progressBar_ = new QProgressBar(this);
    progressBar_->setObjectName(QStringLiteral("updateProgress"));
    progressBar_->setFixedHeight(8);
    progressBar_->setTextVisible(false);
    progressBar_->hide();
    layout->addWidget(progressBar_);
    action_ = new QPushButton(this);
    action_->setObjectName(QStringLiteral("updateAction"));
    layout->addWidget(action_);
    cancel_ = new QPushButton(QStringLiteral("Cancelar download"), this);
    cancel_->setObjectName(QStringLiteral("updateCancel"));
    cancel_->hide();
    layout->addWidget(cancel_);
    QFile input(source);
    if (source.isEmpty() || !input.open(QIODevice::ReadOnly) || input.size() > 1024 * 1024) {
        message_->setText(
            QStringLiteral("O canal de atualização desta edição ainda não foi configurado. "
                           "Você pode continuar usando a versão instalada."));
    } else {
        service_ = std::make_shared<UpdateService>(profile, input.readAll(),
                                                   QCoreApplication::applicationVersion(),
                                                   QStringLiteral("stable"));
        message_->setText(QStringLiteral("Verifique se há uma nova versão do Nodivu."));
    }
    connect(action_, &QPushButton::clicked, this, [this] {
        if (!action_->isEnabled() || busy_)
            return;
        if (!offer_.installer.isEmpty() && offer_.error.isEmpty()) {
            if (QMessageBox::question(
                    this, QStringLiteral("Instalar atualização"),
                    QStringLiteral(
                        "Encerrar o Nodivu e abrir o instalador verificado da versão %1?")
                        .arg(offer_.version),
                    QMessageBox::Yes | QMessageBox::Cancel,
                    QMessageBox::Cancel) == QMessageBox::Yes)
                emit installRequested(offer_.installer, offer_.sha256);
        } else
            run(offer_.available && offer_.error.isEmpty());
    });
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (progress_)
            progress_->cancel();
        setActionAvailability(cancel_, QStringLiteral("Cancelar download"),
                              QStringLiteral("Cancelamento solicitado; aguarde."));
    });
    auto* timer = new QTimer(this);
    timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, [this] {
        if (!busy_ || !progress_)
            return;
        const auto expected = progress_->expected.load();
        if (expected > 0) {
            progressBar_->setRange(0, 100);
            progressBar_->setValue(static_cast<int>(100 * progress_->received.load() / expected));
        }
    });
    connect(timer, &QTimer::timeout, this, [this, timer] {
        if (!busy_)
            timer->stop();
    });
    connect(action_, &QPushButton::clicked, timer, [this, timer] {
        if (busy_)
            timer->start();
    });
    updateAction();
}
UpdatePanel::~UpdatePanel() {
    if (progress_)
        progress_->cancel();
    pool_.waitForDone();
}
void UpdatePanel::setInstallBlockedReason(const QString& reason) {
    installBlockedReason_ = reason;
    updateAction();
}
void UpdatePanel::updateAction() {
    const bool install = !offer_.installer.isEmpty() && offer_.error.isEmpty();
    action_->setText(busy_                     ? QStringLiteral("Aguarde…")
                     : install                 ? QStringLiteral("Reiniciar e atualizar")
                     : !offer_.error.isEmpty() ? QStringLiteral("Tentar novamente")
                     : offer_.available        ? QStringLiteral("Baixar atualização")
                                               : QStringLiteral("Verificar atualização"));
    const auto reason = !service_ ? QStringLiteral("O canal de atualização não está configurado.")
                        : busy_
                            ? QStringLiteral("Uma verificação ou transferência está em andamento.")
                        : install ? installBlockedReason_
                                  : QString();
    setActionAvailability(action_, action_->text(), reason);
}
void UpdatePanel::run(bool download) {
    if (busy_ || !service_)
        return;
    busy_ = true;
    progress_ = std::make_shared<TransferProgress>();
    message_->setText(download ? QStringLiteral("Baixando e verificando o instalador…")
                               : QStringLiteral("Verificando metadados assinados…"));
    progressBar_->setRange(0, 0);
    progressBar_->show();
    cancel_->setVisible(download);
    setActionAvailability(cancel_, QStringLiteral("Cancelar download"));
    updateAction();
    auto* watcher = new QFutureWatcher<UpdateOffer>(this);
    connect(watcher, &QFutureWatcher<UpdateOffer>::finished, this, [this, watcher] {
        offer_ = watcher->result();
        watcher->deleteLater();
        busy_ = false;
        progressBar_->hide();
        cancel_->hide();
        message_->setText(
            !offer_.error.isEmpty() ? offer_.error
            : offer_.available
                ? QStringLiteral("Versão %1 · %2 MiB\n%3")
                      .arg(offer_.version)
                      .arg(offer_.bytes / (1024 * 1024))
                      .arg(offer_.notes)
                : QStringLiteral("Nenhuma versão mais recente disponível neste canal."));
        updateAction();
    });
    watcher->setFuture(
        QtConcurrent::run(&pool_, [service = service_, progress = progress_, download] {
            return download ? service->download(progress.get()) : service->check(progress.get());
        }));
}
} // namespace nodivu
