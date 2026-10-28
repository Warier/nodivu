#pragma once
#include "ui/controls/message_presentation.hpp"
#include "ui/theme/studio_icons.hpp"
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
namespace nodivu {
class FeedbackBanner final : public QFrame {
  public:
    explicit FeedbackBanner(QWidget* parent) : QFrame(parent) {
        setObjectName(QStringLiteral("feedbackBanner"));
        setStyleSheet(QStringLiteral(
            "QFrame#feedbackBanner { background: #3c252e; border-top: 1px solid #80515d; } QLabel "
            "{ color: #ffdade; background: transparent; font-size: 12px; } QToolButton { color: "
            "#ffdade; border: 0; padding: 6px; }"));
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(14, 10, 10, 10);
        auto* row = new QHBoxLayout;
        summary_ = new QLabel(this);
        summary_->setTextFormat(Qt::PlainText);
        summary_->setWordWrap(true);
        summary_->setObjectName(QStringLiteral("feedbackSummary"));
        row->addWidget(summary_, 1);
        expand_ = new QToolButton(this);
        expand_->setText(QStringLiteral("Detalhes"));
        expand_->setCheckable(true);
        expand_->setObjectName(QStringLiteral("feedbackDetailsToggle"));
        row->addWidget(expand_);
        auto* close = new QToolButton(this);
        close->setObjectName(QStringLiteral("feedbackClose"));
        close->setIcon(studioIcon(StudioIcon::Close));
        close->setFixedSize(32, 32);
        close->setAccessibleName(QStringLiteral("Fechar aviso"));
        close->setToolTip(close->accessibleName());
        row->addWidget(close);
        layout->addLayout(row);
        details_ = new QLabel(this);
        details_->setTextFormat(Qt::PlainText);
        details_->setWordWrap(true);
        details_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        details_->hide();
        layout->addWidget(details_);
        connect(expand_, &QToolButton::toggled, details_, &QWidget::setVisible);
        connect(close, &QToolButton::clicked, this, &QWidget::hide);
    }
    void setText(const QString& value) {
        if (raw_ == value)
            return;
        raw_ = value;
        summary_->setText(friendlyError(value));
        details_->setText(value);
        expand_->setChecked(false);
        setAccessibleName(summary_->text());
    }
    QString text() const {
        return raw_;
    }
    void clear() {
        setText({});
        hide();
    }

  private:
    QString raw_;
    QLabel* summary_ = nullptr;
    QLabel* details_ = nullptr;
    QToolButton* expand_ = nullptr;
};
} // namespace nodivu
