#include "features/library/library_panel.hpp"
#include "features/library/library_block_button.hpp"
#include "ui/theme/theme.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace nodivu {
LibraryPanel::LibraryPanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("libraryPanel"));
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet(QStringLiteral("QWidget#libraryPanel { background: %1; }")
                      .arg(Theme::headerBackground().name()));
    setMinimumWidth(184);
    setMaximumWidth(320);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    scroll_ = new QScrollArea(this);
    scroll_->setObjectName(QStringLiteral("libraryScroll"));
    scroll_->setAccessibleName(QStringLiteral("Biblioteca de blocos"));
    scroll_->setWidgetResizable(true);
    scroll_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setStyleSheet(QStringLiteral(
        "QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }"
        "QScrollBar:vertical { background: transparent; width: 6px; }"
        "QScrollBar::handle:vertical { background: #415063; min-height: 24px; border-radius: 3px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; "
        "}"));
    auto* content = new QWidget(scroll_);
    contentLayout_ = new QVBoxLayout(content);
    contentLayout_->setContentsMargins(12, 18, 12, 0);
    contentLayout_->setSpacing(0);
    auto* heading = new QWidget(content);
    auto* headingLayout = new QHBoxLayout(heading);
    headingLayout->setContentsMargins(0, 0, 0, 0);
    headingLabels_ = new QWidget(heading);
    auto* labels = new QVBoxLayout(headingLabels_);
    labels->setContentsMargins(0, 0, 0, 0);
    labels->setSpacing(5);
    auto* eyebrow = new QLabel(QStringLiteral("BIBLIOTECA"), headingLabels_);
    QFont eyebrowFont = eyebrow->font();
    eyebrowFont.setPixelSize(8);
    eyebrowFont.setWeight(QFont::DemiBold);
    eyebrowFont.setLetterSpacing(QFont::AbsoluteSpacing, 2);
    eyebrow->setFont(eyebrowFont);
    eyebrow->setStyleSheet(QStringLiteral("color: #8d9ab0;"));
    labels->addWidget(eyebrow);
    auto* title = new QLabel(QStringLiteral("Blocos"), headingLabels_);
    title->setStyleSheet(QStringLiteral("font-size: 17px; font-weight: 600; color: #e4e8ee;"));
    labels->addWidget(title);
    headingLayout->addWidget(headingLabels_, 1);
    toggle_ = new QToolButton(heading);
    toggle_->setObjectName(QStringLiteral("libraryToggle"));
    toggle_->setText(QStringLiteral("☰"));
    toggle_->setAccessibleName(QStringLiteral("Recolher ou expandir biblioteca"));
    toggle_->setToolTip(QStringLiteral("Recolher biblioteca"));
    toggle_->setCheckable(true);
    toggle_->setFixedSize(32, 32);
    toggle_->setStyleSheet(QStringLiteral(
        "QToolButton { color: #a4afc0; background: transparent; border: 0; font-size: 10px; }"
        "QToolButton:hover, QToolButton:focus { color: #70d9bd; background: #26313e; "
        "border-radius: 5px; }"));
    headingLayout->addWidget(toggle_);
    connect(toggle_, &QToolButton::clicked, this, &LibraryPanel::toggleCollapsed);
    contentLayout_->addWidget(heading);
    intro_ = new QWidget(content);
    auto* introLayout = new QVBoxLayout(intro_);
    introLayout->setContentsMargins(0, 14, 0, 16);
    introLayout->setSpacing(14);
    auto* help = new QLabel(
        QStringLiteral("Arraste um bloco para o projeto\nou clique para adicionar."), intro_);
    help->setWordWrap(true);
    help->setStyleSheet(QStringLiteral("font-size: 11px; color: #95a1b2;"));
    introLayout->addWidget(help);
    auto* virtualOutput =
        new QPushButton(QStringLiteral("↙ Microfone virtual · configurar"), intro_);
    virtualOutput->setObjectName(QStringLiteral("virtualOutputShortcut"));
    virtualOutput_ = virtualOutput;
    virtualOutput->setEnabled(false);
    virtualOutput->setFixedHeight(34);
    virtualOutput->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    virtualOutput->setStyleSheet(
        QStringLiteral("QPushButton { font-size: 10px; background: #252333; border: 1px solid "
                       "#4f4566; padding: 8px; }"
                       "QPushButton:hover, QPushButton:focus { border-color: #70d9bd; }"));
    introLayout->addWidget(virtualOutput);
    connect(virtualOutput, &QPushButton::clicked, this, &LibraryPanel::virtualOutputRequested);
    contentLayout_->addWidget(intro_);
    for (const auto& name :
         {QStringLiteral("Fontes"), QStringLiteral("Processamento"), QStringLiteral("Destinos")}) {
        Category category;
        category.name = name;
        category.container = new QWidget(content);
        auto* groupLayout = new QVBoxLayout(category.container);
        groupLayout->setContentsMargins(0, 4, 0, 0);
        groupLayout->setSpacing(0);
        category.toggle = new QToolButton(category.container);
        category.toggle->setObjectName(QStringLiteral("category:") + name);
        category.toggle->setText(name.toUpper());
        QFont categoryFont = category.toggle->font();
        categoryFont.setLetterSpacing(QFont::AbsoluteSpacing, 1);
        category.toggle->setFont(categoryFont);
        category.toggle->setIconSize(QSize(8, 8));
        category.toggle->setToolTip(name);
        category.toggle->setCheckable(true);
        category.toggle->setChecked(true);
        category.toggle->setArrowType(Qt::DownArrow);
        category.toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        category.toggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        category.toggle->setFixedHeight(26);
        category.toggle->setStyleSheet(
            QStringLiteral("QToolButton { text-align: left; color: #a2afbe; font-size: 10px; "
                           "background: transparent; border: 0; padding: 7px 2px; }"
                           "QToolButton:hover, QToolButton:focus { color: #70d9bd; }"));
        groupLayout->addWidget(category.toggle);
        category.cards = new QWidget(category.container);
        category.layout = new QVBoxLayout(category.cards);
        category.layout->setContentsMargins(0, 8, 0, 16);
        category.layout->setSpacing(8);
        groupLayout->addWidget(category.cards);
        auto* cards = category.cards;
        auto* button = category.toggle;
        connect(button, &QToolButton::toggled, this, [cards, button](bool expanded) {
            cards->setVisible(expanded);
            button->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        });
        contentLayout_->addWidget(category.container);
        categories_.append(category);
    }
    note_ = new QLabel(QStringLiteral("Puxe um fio entre as portas.\nBotão direito no fio para "
                                      "opções.\nGanho e bypass sem parar o áudio."),
                       content);
    note_->setContentsMargins(0, 24, 0, 0);
    note_->setStyleSheet(QStringLiteral("font-size: 10px; color: #95a1b2;"));
    contentLayout_->addWidget(note_);
    contentLayout_->addStretch(1);
    scroll_->setWidget(content);
    layout->addWidget(scroll_, 1);
}

void LibraryPanel::setVirtualOutputEnabled(bool enabled) {
    virtualOutput_->setEnabled(enabled);
}

void LibraryPanel::setCatalog(const NodeCatalog& catalog) {
    for (auto& category : categories_)
        while (auto* item = category.layout->takeAt(0)) {
            delete item->widget();
            delete item;
        }
    for (const auto& descriptor : catalog.entries()) {
        const auto name = descriptor.inputs == 0    ? QStringLiteral("Fontes")
                          : descriptor.outputs == 0 ? QStringLiteral("Destinos")
                                                    : QStringLiteral("Processamento");
        for (auto& category : categories_) {
            if (category.name != name)
                continue;
            auto* button = new LibraryBlockButton(descriptor, category.cards);
            button->setCompact(collapsed_);
            category.layout->addWidget(button);
            connect(button, &QPushButton::clicked, this,
                    [this, kind = descriptor.kind, pluginId = descriptor.pluginId] {
                        emit addRequested(kind, pluginId);
                    });
            break;
        }
    }
    for (const auto& category : categories_)
        category.container->setVisible(category.layout->count() > 0);
}

void LibraryPanel::toggleCollapsed() {
    if (!collapsed_)
        expandedWidth_ = std::clamp(width(), 184, 320);
    collapsed_ = !collapsed_;
    setMinimumWidth(collapsed_ ? 64 : 184);
    setMaximumWidth(collapsed_ ? 64 : 320);
    headingLabels_->setVisible(!collapsed_);
    intro_->setVisible(!collapsed_);
    note_->setVisible(!collapsed_);
    contentLayout_->setContentsMargins(collapsed_ ? 9 : 12, collapsed_ ? 12 : 18,
                                       collapsed_ ? 9 : 12, 0);
    toggle_->setToolTip(collapsed_ ? QStringLiteral("Abrir biblioteca")
                                   : QStringLiteral("Recolher biblioteca"));
    for (auto& category : categories_) {
        if (collapsed_)
            category.expanded = category.toggle->isChecked();
        category.toggle->setVisible(!collapsed_);
        category.toggle->setChecked(collapsed_ || category.expanded);
    }
    for (auto* button : findChildren<LibraryBlockButton*>())
        button->setCompact(collapsed_);
    emit widthRequested(collapsed_ ? 64 : expandedWidth_);
}
} // namespace nodivu
