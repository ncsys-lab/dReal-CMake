//
// Created by Kunal Sheth on 12/31/25.
//

#ifndef DREAL4_CMAKE_CAV26_VARNAME_PARSER_H
#define DREAL4_CMAKE_CAV26_VARNAME_PARSER_H

#include <dreal/version.h> // NOLINT(*-include-cleaner)

#ifdef CAV26_FILTER_SYMMETRIES

#include <cassert>
#include <iostream>
#include <regex>
#include <string>
#include <utility>

#include "dreal/util/assert.h"
#include "dreal/util/exception.h"
#include "dreal/util/logging.h"

namespace dreal
{
    static const std::regex SAR_VARIABLE_RE{R"(^(.+)_t(\d+)(ad|bc)$)"};
    static const std::regex DRH_VARIABLE_RE{R"(^(.+)_(\d+)(_t|_0)?$)"};

    static auto CAV26_SAR_PARSER(const std::string& name) -> std::pair<std::string, double> {
        std::string prefix{};
        double t = -1; // "unknown"

        if (std::smatch m; std::regex_search(name, m, SAR_VARIABLE_RE)) {
            const auto mag = std::stoi(m[2].str());
            const auto polarity = m[3].str();
            prefix = m[1].str();
            t = mag * (polarity == "bc" ? -1 : +1);
        }
        else if (name.rfind("ITE", 0) == 0) { // known issue...
            const auto last = name.rfind("ITE");
            prefix = name; // "ITE";
            // t is unknown ... // if (last != std::string::npos) t = std::stoi(name.substr(last + 3));
        }
        else throw DREAL_RUNTIME_ERROR("Invalid SAR variable name: {}", name);

        return {std::move(prefix), t};
    }

    static auto CAV26_DRH_PARSER(const std::string& name) -> std::pair<std::string, double> {
        if (std::smatch m; std::regex_search(name, m, DRH_VARIABLE_RE)) {
            const auto mag = std::stoi(m[2].str());
            const auto step = m[3].matched ? m[3].str() : std::string{};

            auto prefix = m[1].str();
            const auto t = mag + (step == "_t" ? 0.5 : 0.0);
            return {std::move(prefix), t};
        }
        throw DREAL_RUNTIME_ERROR("Invalid DRH variable name: {}", name);
    }
} // namespace dreal

#endif //CAV26_FILTER_SYMMETRIES
#endif //DREAL4_CMAKE_CAV26_VARNAME_PARSER_H
