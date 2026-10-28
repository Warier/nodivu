#include "features/graph/routing_panel.hpp"
#include "ui/controls/action_availability.hpp"
#include "ui/theme/studio_icons.hpp"
#include "ui/theme/studio_panel_style.hpp"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace nodivu {
namespace {
QString title(const SessionController& session, const QJsonObject& names, const QString& id) {
    const auto descriptor = session.catalog().describe(session.node(id));
    auto base =
        descriptor.title.isEmpty() ? QStringLiteral("Bloco indisponível") : descriptor.title;
    if (!names.value(id).toString().isEmpty())
        base = names.value(id).toString();
    int index = 0;
    for (const auto& node : session.snapshot()
                                .value(QStringLiteral("graph"))
                                .toObject()
                                .value(QStringLiteral("nodes"))
                                .toArray()) {
        ++index;
        if (node.toObject().value(QStringLiteral("id")).toString() == id)
            return QStringLiteral("%1 · bloco %2").arg(base).arg(index);
    }
    return base;
}
QString inputLabel(const NodeDescriptor& descriptor, int port) {
    return descriptor.inputLabels.value(port, QStringLiteral("Entrada %1").arg(port + 1));
}
} // namespace
RoutingPanel::RoutingPanel(SessionController& session, QWidget* parent)
    : QWidget(parent), session_(session) {
    setObjectName(QStringLiteral("accessibleRouting"));
    setMinimumWidth(340);
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet(studioPanelStyle() +
                  QStringLiteral("QWidget#accessibleRouting { background: #19222c; }"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);
    auto* heading = new QLabel(QStringLiteral("BLOCOS E CONEXÕES"), this);
    heading->setProperty("role", QStringLiteral("eyebrow"));
    layout->addWidget(heading);
    auto* help = new QLabel(QStringLiteral("Navegue pelos blocos e conexões com as setas. Enter "
                                           "abre as configurações do bloco selecionado."),
                            this);
    help->setWordWrap(true);
    layout->addWidget(help);
    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("routingTree"));
    tree_->setHeaderLabels({QStringLiteral("Bloco / conexão"), QStringLiteral("Tipo")});
    tree_->setAccessibleName(QStringLiteral("Blocos e conexões"));
    tree_->setAccessibleDescription(help->text());
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->setUniformRowHeights(false);
    tree_->setWordWrap(true);
    tree_->setTextElideMode(Qt::ElideRight);
    tree_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    tree_->header()->resizeSection(1, 96);
    layout->addWidget(tree_, 1);
    inspect_ = new QPushButton(QStringLiteral("&Configurar bloco"), this);
    connect_ = new QPushButton(QStringLiteral("Co&nectar blocos…"), this);
    remove_ = new QPushButton(QStringLiteral("&Remover seleção"), this);
    inspect_->setObjectName(QStringLiteral("routingInspect"));
    connect_->setObjectName(QStringLiteral("routingConnect"));
    remove_->setObjectName(QStringLiteral("routingRemove"));
    remove_->setProperty("role", QStringLiteral("danger"));
    for (auto* button : {inspect_, connect_, remove_})
        layout->addWidget(button);
    auto* note =
        new QLabel(QStringLiteral("As alterações são aplicadas ao mesmo grafo do projeto."), this);
    note->setWordWrap(true);
    layout->addWidget(note);
    connect(inspect_, &QPushButton::clicked, this, [this] {
        if (const auto* item = tree_->currentItem())
            emit inspectRequested(item->data(0, Qt::UserRole).toString());
    });
    connect(tree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        emit inspectRequested(item->data(0, Qt::UserRole).toString());
    });
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, &RoutingPanel::updateActions);
    connect(connect_, &QPushButton::clicked, this, &RoutingPanel::connectSelected);
    connect(remove_, &QPushButton::clicked, this, [this] {
        const auto* item = tree_->currentItem();
        if (!item || !session_.settled() || !blockedReason_.isEmpty())
            return;
        const auto edge = item->data(0, Qt::UserRole + 1).toJsonObject();
        if (!edge.isEmpty())
            session_.disconnectEdge(edge);
        else
            session_.removeNode(item->data(0, Qt::UserRole).toString());
    });
    connect(&session_, &SessionController::changed, this, &RoutingPanel::synchronize);
    connect(&session_, &SessionController::activityChanged, this, &RoutingPanel::updateActions);
    connect(&session_, &SessionController::catalogChanged, this, [this] {
        graph_ = {};
        availabilityGraph_ = {};
        synchronize();
    });
    updateActions();
}
void RoutingPanel::setBlockedReason(const QString& reason) {
    blockedReason_ = reason;
    updateActions();
}
void RoutingPanel::setDisplayNames(const QJsonObject& names) {
    if (names_ == names)
        return;
    names_ = names;
    graph_ = {};
    synchronize();
}
void RoutingPanel::showEvent(QShowEvent* event) {
    synchronize();
    QWidget::showEvent(event);
}
void RoutingPanel::synchronize() {
    if (!isVisible())
        return;
    const auto graph = session_.snapshot().value(QStringLiteral("graph")).toObject();
    if (graph == graph_) {
        updateActions();
        return;
    }
    const auto* selected = tree_->currentItem();
    const auto selectedId = selected ? selected->data(0, Qt::UserRole).toString() : QString();
    const auto selectedEdge =
        selected ? selected->data(0, Qt::UserRole + 1).toJsonObject() : QJsonObject();
    graph_ = graph;
    QSignalBlocker blocker(tree_);
    tree_->clear();
    QHash<QString, QTreeWidgetItem*> nodes;
    for (const auto& value : graph.value(QStringLiteral("nodes")).toArray()) {
        const auto node = value.toObject();
        const auto id = node.value(QStringLiteral("id")).toString();
        const auto descriptor = session_.catalog().describe(node);
        auto* item = new QTreeWidgetItem(tree_, {title(session_, names_, id), descriptor.subtitle});
        item->setIcon(0, studioIcon(blockIcon(descriptor), descriptor.accent));
        item->setForeground(0, descriptor.accent);
        item->setData(0, Qt::UserRole, id);
        item->setToolTip(0, descriptor.description);
        item->setExpanded(true);
        nodes.insert(id, item);
        if (selectedId == id && selectedEdge.isEmpty())
            tree_->setCurrentItem(item);
    }
    for (const auto& value : graph.value(QStringLiteral("edges")).toArray()) {
        const auto edge = value.toObject();
        const auto from = edge.value(QStringLiteral("from")).toString();
        const auto to = edge.value(QStringLiteral("to")).toString();
        if (!nodes.contains(from))
            continue;
        const auto descriptor = session_.catalog().describe(session_.node(to));
        auto* item = new QTreeWidgetItem(
            nodes.value(from),
            {QStringLiteral("Saída %1 → %2 · %3")
                 .arg(edge.value(QStringLiteral("from_port")).toInt() + 1)
                 .arg(title(session_, names_, to),
                      inputLabel(descriptor, edge.value(QStringLiteral("to_port")).toInt())),
             QStringLiteral("Conexão")});
        item->setData(0, Qt::UserRole, from);
        item->setData(0, Qt::UserRole + 1, edge);
        if (edge == selectedEdge)
            tree_->setCurrentItem(item);
    }
    if (!tree_->currentItem() && tree_->topLevelItemCount() > 0)
        tree_->setCurrentItem(tree_->topLevelItem(0));
    updateActions();
}
void RoutingPanel::updateActions() {
    const auto* item = tree_->currentItem();
    const auto reason = !blockedReason_.isEmpty() ? blockedReason_
                        : !session_.online() ? QStringLiteral("O motor de áudio está desconectado.")
                        : !session_.settled()
                            ? QStringLiteral("Aguarde a operação atual do projeto.")
                            : QString();
    setActionAvailability(inspect_, QStringLiteral("Abrir as configurações do bloco selecionado"),
                          !reason.isEmpty() ? reason
                          : !item           ? QStringLiteral("Selecione um bloco na lista.")
                                            : QString());
    setActionAvailability(remove_, QStringLiteral("Remover o bloco ou a conexão selecionada"),
                          !reason.isEmpty() ? reason
                          : !item ? QStringLiteral("Selecione um bloco ou uma conexão na lista.")
                                  : QString());
    const auto graph = session_.snapshot().value(QStringLiteral("graph")).toObject();
    const auto nodes = graph.value(QStringLiteral("nodes")).toArray();
    const auto edges = graph.value(QStringLiteral("edges")).toArray();
    if (availabilityGraph_ != graph) {
        availabilityGraph_ = graph;
        possibleConnection_ = false;
        for (const auto& sourceValue : nodes) {
            if (possibleConnection_)
                break;
            const auto source = sourceValue.toObject();
            if (session_.catalog().describe(source).outputs == 0)
                continue;
            for (const auto& targetValue : nodes) {
                if (possibleConnection_)
                    break;
                const auto target = targetValue.toObject();
                if (source.value(QStringLiteral("id")) == target.value(QStringLiteral("id")))
                    continue;
                for (int port = 0; port < session_.catalog().describe(target).inputs; ++port) {
                    const bool occupied =
                        std::any_of(edges.cbegin(), edges.cend(), [&](const QJsonValue& value) {
                            const auto edge = value.toObject();
                            return edge.value(QStringLiteral("to")) ==
                                       target.value(QStringLiteral("id")) &&
                                   edge.value(QStringLiteral("to_port")).toInt() == port;
                        });
                    if (!occupied) {
                        possibleConnection_ = true;
                        break;
                    }
                }
            }
        }
    }
    setActionAvailability(
        connect_, QStringLiteral("Conectar blocos por origem, destino e portas"),
        !reason.isEmpty() ? reason
        : !possibleConnection_
            ? QStringLiteral("Adicione uma origem e um destino com uma entrada livre.")
            : QString());
}
void RoutingPanel::connectSelected() {
    if (!session_.settled() || !blockedReason_.isEmpty())
        return;
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("connectBlocksDialog"));
    dialog.setStyleSheet(studioPanelStyle() + QStringLiteral("QDialog { background: #19222c; }"));
    dialog.setWindowTitle(QStringLiteral("Conectar blocos"));
    auto* form = new QFormLayout(&dialog);
    form->setContentsMargins(20, 20, 20, 20);
    form->setSpacing(12);
    QComboBox from, fromPort, to, toPort;
    from.setAccessibleName(QStringLiteral("Bloco de origem"));
    fromPort.setAccessibleName(QStringLiteral("Porta de saída"));
    to.setAccessibleName(QStringLiteral("Bloco de destino"));
    toPort.setAccessibleName(QStringLiteral("Porta de entrada"));
    form->addRow(QStringLiteral("&Origem"), &from);
    form->addRow(QStringLiteral("&Saída"), &fromPort);
    form->addRow(QStringLiteral("&Destino"), &to);
    form->addRow(QStringLiteral("&Entrada"), &toPort);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons.button(QDialogButtonBox::Ok)->setText(QStringLiteral("Conectar"));
    form->addRow(&buttons);
    for (const auto& value : session_.snapshot()
                                 .value(QStringLiteral("graph"))
                                 .toObject()
                                 .value(QStringLiteral("nodes"))
                                 .toArray()) {
        const auto node = value.toObject();
        const auto descriptor = session_.catalog().describe(node);
        const auto id = node.value(QStringLiteral("id")).toString();
        if (descriptor.outputs > 0)
            from.addItem(title(session_, names_, id), id);
        if (descriptor.inputs > 0)
            to.addItem(title(session_, names_, id), id);
    }
    const auto updatePorts = [&] {
        const auto source =
            session_.catalog().describe(session_.node(from.currentData().toString()));
        const auto destination =
            session_.catalog().describe(session_.node(to.currentData().toString()));
        const int oldFrom = fromPort.currentData().toInt();
        const int oldTo = toPort.currentData().toInt();
        QSignalBlocker blockFrom(fromPort), blockTo(toPort);
        fromPort.clear();
        toPort.clear();
        for (int index = 0; index < source.outputs; ++index)
            fromPort.addItem(QStringLiteral("Saída %1").arg(index + 1), index);
        for (int index = 0; index < destination.inputs; ++index) {
            bool occupied = false;
            for (const auto& value : session_.snapshot()
                                         .value(QStringLiteral("graph"))
                                         .toObject()
                                         .value(QStringLiteral("edges"))
                                         .toArray()) {
                const auto edge = value.toObject();
                occupied |=
                    edge.value(QStringLiteral("to")).toString() == to.currentData().toString() &&
                    edge.value(QStringLiteral("to_port")).toInt() == index;
            }
            toPort.addItem(inputLabel(destination, index) +
                               (occupied ? QStringLiteral(" · ocupada") : QString()),
                           index);
            if (occupied)
                qobject_cast<QStandardItemModel*>(toPort.model())->item(index)->setEnabled(false);
        }
        fromPort.setCurrentIndex(std::max(0, fromPort.findData(oldFrom)));
        int target = toPort.findData(oldTo);
        const auto* model = qobject_cast<QStandardItemModel*>(toPort.model());
        if (target < 0 || !model->item(target)->isEnabled()) {
            target = -1;
            for (int index = 0; index < toPort.count(); ++index)
                if (model->item(index)->isEnabled()) {
                    target = index;
                    break;
                }
        }
        toPort.setCurrentIndex(target);
        const bool ready =
            !from.currentData().toString().isEmpty() && !to.currentData().toString().isEmpty() &&
            from.currentData() != to.currentData() && target >= 0 && fromPort.currentIndex() >= 0;
        setActionAvailability(
            buttons.button(QDialogButtonBox::Ok), QStringLiteral("Conectar as portas selecionadas"),
            ready ? QString() : QStringLiteral("Escolha blocos diferentes e uma entrada livre."));
    };
    connect(&from, &QComboBox::currentIndexChanged, &dialog, updatePorts);
    connect(&to, &QComboBox::currentIndexChanged, &dialog, updatePorts);
    if (const auto* selected = tree_->currentItem())
        from.setCurrentIndex(std::max(0, from.findData(selected->data(0, Qt::UserRole))));
    updatePorts();
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted)
        session_.connectNodes(from.currentData().toString(), fromPort.currentData().toInt(),
                              to.currentData().toString(), toPort.currentData().toInt());
}
} // namespace nodivu
