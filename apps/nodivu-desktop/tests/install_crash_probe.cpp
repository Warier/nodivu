#include "platform/install_transaction.hpp"
#include <QCoreApplication>
#include <cstdlib>

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    if (arguments.size() != 3)
        return 2;
    nodivu::InstallTransaction transaction(arguments[1]);
    transaction.apply(arguments[2], [](const QString&) -> QString { std::_Exit(73); });
    return 1;
}
