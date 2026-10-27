#pragma once
#include <QString>

namespace nodivu::windows {
void initializeDesktopIdentity();
void applyWindowTheme(quintptr handle);
QString legacyProfileConflict();
} // namespace nodivu::windows
