#include "features/hotkeys/hotkey_service.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QUuid>
#include <QVBoxLayout>
namespace nodivu {
void showHotkeys(HotkeyService& service, QWidget* parent) {
    service.suspend(true);
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("hotkeyDialog"));
    dialog.setWindowTitle(QStringLiteral("Atalhos e macros · Nodivu Qt"));
    dialog.resize(780, 570);
    auto* layout = new QVBoxLayout(&dialog);
    auto* hint = new QLabel(
        QStringLiteral("Atalhos globais funcionam com o Nodivu minimizado. Macros do mouse podem "
                       "enviar F13–F24.\nEnquanto esta janela estiver aberta, os atalhos ficam "
                       "suspensos. Nenhum atalho vem ativado por padrão."),
        &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto* enabled = new QCheckBox(QStringLiteral("Habilitar meus atalhos"), &dialog);
    enabled->setChecked(service.config().value(QStringLiteral("enabled")).toBool());
    layout->addWidget(enabled);
    auto* list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("hotkeyBindings"));
    layout->addWidget(list);
    auto* form = new QFormLayout;
    auto* actions = new QComboBox(&dialog);
    actions->setObjectName(QStringLiteral("hotkeyAction"));
    const auto available = service.actions();
    for (const auto& a : available)
        actions->addItem(a.label, a.id);
    auto* scope = new QComboBox(&dialog);
    scope->addItem(QStringLiteral("Global (Windows)"), QStringLiteral("global"));
    scope->addItem(QStringLiteral("Somente no Nodivu"), QStringLiteral("local"));
    auto* key = new QKeySequenceEdit(&dialog);
    key->setMaximumSequenceLength(1);
    key->setObjectName(QStringLiteral("hotkeyKey"));
    auto* value = new QDoubleSpinBox(&dialog);
    value->setRange(-1000000, 1000000);
    value->setDecimals(4);
    value->setObjectName(QStringLiteral("hotkeyValue"));
    form->addRow(QStringLiteral("Ação / bloco"), actions);
    form->addRow(QStringLiteral("Alcance"), scope);
    form->addRow(QStringLiteral("Pressione as teclas"), key);
    form->addRow(QStringLiteral("Valor (definir / somar)"), value);
    layout->addLayout(form);
    auto* feedback = new QLabel(service.loadError(), &dialog);
    feedback->setTextFormat(Qt::PlainText);
    feedback->setWordWrap(true);
    layout->addWidget(feedback);
    auto* row = new QHBoxLayout;
    auto* add = new QPushButton(QStringLiteral("Adicionar"), &dialog);
    auto* remove = new QPushButton(QStringLiteral("Remover selecionado"), &dialog);
    auto* test = new QPushButton(QStringLiteral("Testar selecionado"), &dialog);
    row->addWidget(add);
    row->addWidget(remove);
    row->addWidget(test);
    layout->addLayout(row);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(buttons);
    const auto refresh = [&] {
        const auto selected = list->currentRow();
        list->clear();
        for (const auto& item : service.config().value(QStringLiteral("bindings")).toArray()) {
            const auto b = item.toObject();
            auto* entry = new QListWidgetItem(
                b.value(QStringLiteral("accelerator")).toString() + QStringLiteral(" · ") +
                    b.value(QStringLiteral("label")).toString() + QStringLiteral("\n") +
                    service.state(b.value(QStringLiteral("id")).toString()),
                list);
            entry->setData(Qt::UserRole, b.value(QStringLiteral("id")).toString());
        }
        if (selected >= 0 && selected < list->count())
            list->setCurrentRow(selected);
    };
    const auto save = [&](QJsonObject config) {
        const auto error = service.save(config);
        feedback->setText(
            error.isEmpty()
                ? QStringLiteral("Salvo. Feche esta janela para ativar e verificar conflitos.")
                : error);
        refresh();
    };
    QObject::connect(enabled, &QCheckBox::toggled, &dialog, [&](bool checked) {
        auto config = service.config();
        config.insert(QStringLiteral("enabled"), checked);
        save(config);
    });
    QObject::connect(actions, &QComboBox::currentIndexChanged, &dialog, [&](int index) {
        value->setEnabled(index >= 0 && available[index].needsValue);
    });
    value->setEnabled(!available.isEmpty() && available.first().needsValue);
    QObject::connect(add, &QPushButton::clicked, &dialog, [&] {
        if (actions->currentIndex() < 0)
            return;
        const auto a = available[actions->currentIndex()];
        QJsonObject b{
            {QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {QStringLiteral("enabled"), true},
            {QStringLiteral("scope"), scope->currentData().toString()},
            {QStringLiteral("action"), a.id},
            {QStringLiteral("label"), a.label},
            {QStringLiteral("accelerator"), HotkeyService::accelerator(key->keySequence())}};
        if (a.needsValue)
            b.insert(QStringLiteral("value"), value->value());
        auto config = service.config();
        auto bindings = config.value(QStringLiteral("bindings")).toArray();
        bindings.append(b);
        config.insert(QStringLiteral("bindings"), bindings);
        save(config);
    });
    QObject::connect(remove, &QPushButton::clicked, &dialog, [&] {
        if (list->currentRow() < 0)
            return;
        auto config = service.config();
        auto bindings = config.value(QStringLiteral("bindings")).toArray();
        bindings.removeAt(list->currentRow());
        config.insert(QStringLiteral("bindings"), bindings);
        save(config);
    });
    QObject::connect(test, &QPushButton::clicked, &dialog, [&] {
        if (list->currentItem())
            service.fire(list->currentItem()->data(Qt::UserRole).toString(), true);
    });
    QObject::connect(&service, &HotkeyService::result, &dialog,
                     [&](bool, const QString& text) { feedback->setText(text); });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    refresh();
    dialog.exec();
    // exec() returns on Close, Escape and title-bar close; registration resumes synchronously.
    service.suspend(false);
    QStringList conflicts;
    for (const auto& item : service.config().value(QStringLiteral("bindings")).toArray()) {
        const auto b = item.toObject();
        const auto state = service.state(b.value(QStringLiteral("id")).toString());
        if (state.startsWith(QStringLiteral("Conflito")))
            conflicts.append(b.value(QStringLiteral("accelerator")).toString() +
                             QStringLiteral(": ") + state);
    }
    if (!conflicts.isEmpty())
        emit service.result(false, conflicts.join(QLatin1Char('\n')));
}
} // namespace nodivu
