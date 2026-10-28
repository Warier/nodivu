#pragma once
#include "contracts/node_descriptor.hpp"
namespace nodivu {
QString nodeSignalStatus(const QJsonObject& snapshot, const NodeDescriptor& descriptor,
                         const QJsonObject& node, const QJsonArray& devices, bool online,
                         double peak);
}
