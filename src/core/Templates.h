#pragma once

#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx
{
// New project templates (File > New from template).
std::vector<std::string> templateNames();
Song makeTemplate (int index);
} // namespace lynx
