#include "features/library/library_panel.hpp"
#include "features/library/library_block_button.hpp"
#include "ui/controls/search_text.hpp"
#include "ui/theme/studio_icons.hpp"
#include "ui/theme/theme.hpp"
#include <QAction>
#include <QJsonArray>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QStyleOptionButton>

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace nodivu {
namespace {
class OutputShortcutButton final : public QPushButton {
  public:
    using QPushButton::QPushButton;

  protected:
    void paintEvent(QPaintEvent*) override {
        QStyleOptionButton option;
        initStyleOption(&option);
        const auto contents = style()->subElementRect(QStyle::SE_PushButtonContents, &option, this);
        // Keep the native button states and a consistent gap independent of Qt's icon spacing.
        option.text.clear();
        option.icon = QIcon();
        QPainter painter(this);
        style()->drawControl(QStyle::CE_PushButton, &option, &painter, this);
        const QRect iconBounds(contents.left(), contents.center().y() - iconSize().height() / 2,
                               iconSize().width(), iconSize().height());
        icon().paint(&painter, iconBounds, Qt::AlignCenter,
                     isEnabled() ? QIcon::Normal : QIcon::Disabled);
        const auto textBounds = contents.adjusted(iconSize().width() + 10, 0, 0, 0);
        painter.setPen(option.palette.color(QPalette::ButtonText));
        painter.drawText(textBounds, Qt::AlignLeft | Qt::AlignVCenter,
                         fontMetrics().elidedText(text(), Qt::ElideRight, textBounds.width()));
    }
};
} // namespace

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
    toggle_->setIcon(studioIcon(StudioIcon::Menu));
    toggle_->setAccessibleName(QStringLiteral("Recolher ou expandir biblioteca"));
    toggle_->setToolTip(QStringLiteral("Recolher biblioteca"));
    toggle_->setCheckable(true);
    toggle_->setFixedSize(32, 32);
    toggle_->setStyleSheet(QStringLiteral(
        "QToolButton { color: #a4afc0; background: transparent; border: 0; font-size: 11px; }"
        "QToolButton:hover, QToolButton:focus { color: #70d9bd; background: #26313e; "
        "border-radius: 5px; }"));
    headingLayout->addWidget(toggle_);
    connect(toggle_, &QToolButton::clicked, this, &LibraryPanel::toggleCollapsed);
    contentLayout_->addWidget(heading);
    search_ = new QLineEdit(content);
    search_->setObjectName(QStringLiteral("blockSearch"));
    search_->setAccessibleName(QStringLiteral("Buscar blocos"));
    search_->setPlaceholderText(QStringLiteral("Buscar blocos…"));
    search_->setClearButtonEnabled(true);
    search_->addAction(studioIcon(StudioIcon::Search), QLineEdit::LeadingPosition);
    search_->setMinimumHeight(34);
    contentLayout_->addSpacing(12);
    contentLayout_->addWidget(search_);
    noResults_ =
        new QLabel(QStringLiteral("Nenhum bloco encontrado. Tente outro nome ou função."), content);
    noResults_->setObjectName(QStringLiteral("blockSearchEmpty"));
    noResults_->setWordWrap(true);
    noResults_->hide();
    contentLayout_->addWidget(noResults_);
    connect(search_, &QLineEdit::textChanged, this, &LibraryPanel::filterBlocks);
    intro_ = new QWidget(content);
    auto* introLayout = new QVBoxLayout(intro_);
    introLayout->setContentsMargins(0, 14, 0, 16);
    introLayout->setSpacing(14);
    auto* help = new QLabel(
        QStringLiteral("Arraste um bloco para o projeto\nou clique para adicionar."), intro_);
    help->setWordWrap(true);
    help->setStyleSheet(QStringLiteral("font-size: 11px; color: #95a1b2;"));
    introLayout->addWidget(help);
    auto* virtualOutput = new OutputShortcutButton(QStringLiteral("Configurar saída"), intro_);
    virtualOutput->setIcon(studioIcon(StudioIcon::Output, QColor(255, 255, 255)));
    virtualOutput->setIconSize(QSize(18, 18));
    virtualOutput->setToolTip(
        QStringLiteral("Escolher fones, alto-falantes ou um cabo virtual opcional."));
    virtualOutput->setObjectName(QStringLiteral("virtualOutputShortcut"));
    virtualOutput_ = virtualOutput;
    virtualOutput->setEnabled(false);
    virtualOutput->setFixedHeight(38);
    virtualOutput->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    virtualOutput->setStyleSheet(
        QStringLiteral("QPushButton#virtualOutputShortcut { font-size: 12px; text-align: left; "
                       "color: #dbe3ef; background: #252333; border: 1px solid #4f4566; "
                       "border-radius: 7px; padding: 8px 12px; }"
                       "QPushButton#virtualOutputShortcut:hover { background: #302b40; "
                       "border-color: #75658f; }"
                       "QPushButton#virtualOutputShortcut:focus { border-color: #70d9bd; }"
                       "QPushButton#virtualOutputShortcut:pressed { background: #211d2e; }"
                       "QPushButton#virtualOutputShortcut:disabled { color: #758193; "
                       "background: transparent; border-color: #2b3543; }"));
    introLayout->addWidget(virtualOutput);
    connect(virtualOutput, &QPushButton::clicked, this, &LibraryPanel::virtualOutputRequested);
    contentLayout_->addWidget(intro_);
    for (const auto& name : {QStringLiteral("Favoritos"), QStringLiteral("Fontes"),
                             QStringLiteral("Processamento"), QStringLiteral("Destinos")}) {
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
            QStringLiteral("QToolButton { text-align: left; color: #a2afbe; font-size: 11px; "
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
        connect(button, &QToolButton::clicked, this, &LibraryPanel::preferencesChanged);
        contentLayout_->addWidget(category.container);
        categories_.append(category);
    }
    contentLayout_->addStretch(1);
    scroll_->setWidget(content);
    layout->addWidget(scroll_, 1);
}

void LibraryPanel::setVirtualOutputEnabled(bool enabled) {
    virtualOutput_->setEnabled(enabled);
}

void LibraryPanel::setCatalog(const NodeCatalog& catalog) {
    catalog_ = catalog;
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
            const bool favoriteCategory = category.name == QStringLiteral("Favoritos");
            if (favoriteCategory ? !favorites_.contains(descriptor.catalogKey())
                                 : category.name != name)
                continue;
            auto* button = new LibraryBlockButton(descriptor, category.cards);
            if (favoriteCategory)
                button->setObjectName(QStringLiteral("favorite:") + descriptor.catalogKey());
            button->setProperty("favorite", favorites_.contains(descriptor.catalogKey()));
            button->setContextMenuPolicy(Qt::ActionsContextMenu);
            auto* favorite = new QAction(favorites_.contains(descriptor.catalogKey())
                                             ? QStringLiteral("Remover dos favoritos")
                                             : QStringLiteral("Adicionar aos favoritos"),
                                         button);
            button->addAction(favorite);
            connect(favorite, &QAction::triggered, this, [this, key = descriptor.catalogKey()] {
                auto favorites = favorites_;
                if (favorites.contains(key))
                    favorites.removeAll(key);
                else
                    favorites.append(key);
                // Rebuild after the context menu has released its source action.
                QTimer::singleShot(0, this, [this, favorites] {
                    setFavorites(favorites);
                    emit preferencesChanged();
                });
            });
            button->setProperty("searchTerms", blockSearchText(descriptor));
            button->setCompact(collapsed_);
            category.layout->addWidget(button);
            connect(button, &QPushButton::clicked, this,
                    [this, kind = descriptor.kind, pluginId = descriptor.pluginId] {
                        emit addRequested(kind, pluginId);
                    });
        }
    }
    filterBlocks();
}

void LibraryPanel::focusSearch() {
    if (collapsed_)
        toggleCollapsed();
    scroll_->ensureWidgetVisible(search_);
    search_->setFocus(Qt::ShortcutFocusReason);
    search_->selectAll();
}
void LibraryPanel::filterBlocks() {
    int visible = 0;
    for (const auto& category : categories_) {
        bool found = false;
        for (int i = 0; i < category.layout->count(); ++i) {
            auto* button = category.layout->itemAt(i)->widget();
            const bool match =
                matchesSearchText(button->property("searchTerms").toString(), search_->text());
            button->setVisible(match);
            found |= match;
            visible += match ? 1 : 0;
        }
        category.container->setVisible(found);
        if (!search_->text().trimmed().isEmpty())
            category.toggle->setChecked(true);
    }
    noResults_->setVisible(visible == 0);
}

void LibraryPanel::toggleCollapsed() {
    if (!collapsed_)
        expandedWidth_ = std::clamp(width(), 184, 320);
    collapsed_ = !collapsed_;
    setMinimumWidth(collapsed_ ? 64 : 184);
    setMaximumWidth(collapsed_ ? 64 : 320);
    search_->setVisible(!collapsed_);
    if (collapsed_)
        search_->clear();
    headingLabels_->setVisible(!collapsed_);
    intro_->setVisible(!collapsed_);
    updateMargins();
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
    emit preferencesChanged();
}
QStringList LibraryPanel::favorites() const {
    return favorites_;
}
void LibraryPanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateMargins();
}
void LibraryPanel::updateMargins() {
    if (!contentLayout_)
        return;
    const int horizontal = collapsed_ ? 9 : width() < 208 ? 8 : 12;
    contentLayout_->setContentsMargins(horizontal, collapsed_ ? 12 : 18, horizontal, 0);
}
void LibraryPanel::setFavorites(const QStringList& favorites) {
    favorites_ = favorites.mid(0, 128);
    favorites_.removeDuplicates();
    setCatalog(catalog_);
}
QJsonObject LibraryPanel::workspaceState() const {
    QJsonObject categories;
    for (const auto& category : categories_)
        categories.insert(category.name,
                          collapsed_ ? category.expanded : category.toggle->isChecked());
    QJsonArray favorites;
    for (const auto& key : favorites_)
        favorites.append(key);
    return {{QStringLiteral("collapsed"), collapsed_},
            {QStringLiteral("width"), collapsed_ ? expandedWidth_ : width()},
            {QStringLiteral("categories"), categories},
            {QStringLiteral("favorites"), favorites}};
}
void LibraryPanel::restoreWorkspaceState(const QJsonObject& state) {
    QStringList favorites;
    for (const auto& value : state.value(QStringLiteral("favorites")).toArray())
        if (value.isString() && value.toString().size() <= 256)
            favorites.append(value.toString());
    setFavorites(favorites);
    const auto categories = state.value(QStringLiteral("categories")).toObject();
    for (auto& category : categories_)
        category.toggle->setChecked(categories.value(category.name).toBool(true));
    expandedWidth_ = std::clamp(state.value(QStringLiteral("width")).toInt(248), 184, 320);
    emit widthRequested(expandedWidth_);
    if (state.value(QStringLiteral("collapsed")).toBool() != collapsed_)
        toggleCollapsed();
}
} // namespace nodivu
