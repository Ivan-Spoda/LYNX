#pragma once

#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx
{
// Modelos de projeto novo (Arquivo > Novo a partir de modelo).
std::vector<std::string> templateNames();
Song makeTemplate (int index);
} // namespace lynx
