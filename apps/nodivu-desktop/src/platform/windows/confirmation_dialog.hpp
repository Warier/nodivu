#pragma once
#include <QString>
#include <optional>

namespace nodivu::windows {
// Nullopt means native presentation failed; callers must show a fallback.
std::optional<bool> showConfirmation(quintptr parent, const QString& message, const QString& cancel,
                                     const QString& proceed);
} // namespace nodivu::windows
