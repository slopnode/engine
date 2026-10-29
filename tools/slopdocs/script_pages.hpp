#pragma once

#include "site.hpp"

#include <string>

namespace slopdocs {

bool buildScriptPages(Site& site, std::string& error);

std::string scriptAnchor(const std::string& name);

}
