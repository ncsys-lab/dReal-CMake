//
// Created by Kunal Sheth on 12/31/25.
//

#ifndef DREAL4_CMAKE_CAV26_VARNAME_PARSER_H
#define DREAL4_CMAKE_CAV26_VARNAME_PARSER_H

#include <dreal/version.h> // NOLINT(*-include-cleaner)

#if CAV26_FILTER_SYMMETRIES

#include <optional>
#include <unordered_map>
#include <regex>
#include <string>
#include <utility>

#include "dreal/util/exception.h"
#include "dreal/util/logging.h"

namespace dreal
{
    static const std::regex SAR_VARIABLE_RE{R"(^(.+)_t(\d+)(ad|bc)$)"};
    static const std::regex DRH_VARIABLE_RE{R"(^(.+)_(\d+)(_t|_0)?$)"};

    static std::pair<std::string, std::optional<int>> const& CAV26_SAR_PARSER(const std::string& name) {
        static std::unordered_map<std::string, std::pair<std::string, std::optional<int>>> cache;
        const auto cache_it = cache.find(name);
        if (cache_it != cache.end()) return cache_it->second;

        std::string prefix{};
        std::optional<int> t{};

        if (std::smatch m; std::regex_search(name, m, SAR_VARIABLE_RE)) {
            const auto mag = std::stoi(m[2].str());
            const auto polarity = m[3].str();
            prefix = m[1].str();
            t = mag * (polarity == "bc" ? -1 : +1);
        }
        else if (name.rfind("ITE", 0) == 0) { // known issue...
            prefix = name; // "ITE";
            t = std::nullopt; // const auto last = name.rfind("ITE"); if (last != std::string::npos) t = std::stoi(name.substr(last + 3));
        }
        else throw DREAL_RUNTIME_ERROR("Invalid SAR variable name: {}", name);

        return cache.try_emplace(name, std::move(prefix), t).first->second;
    }

    static auto CAV26_DRH_PARSER(const std::string& name) -> std::pair<std::string, std::optional<int>> {
        static std::unordered_map<std::string, std::pair<std::string, std::optional<int>>> cache;
        const auto cache_it = cache.find(name);
        if (cache_it != cache.end()) return cache_it->second;

        if (std::smatch m; std::regex_search(name, m, DRH_VARIABLE_RE)) {
            const int mag = std::stoi(m[2].str());
            const auto step = m[3].matched ? m[3].str() : std::string{};

            auto prefix = m[1].str();
            const int t = (2 * mag) + (step == "_t" ? 1 : 0);
            return cache.try_emplace(name, std::move(prefix), t).first->second;
        }
        throw DREAL_RUNTIME_ERROR("Invalid DRH variable name: {}", name);
    }
} // namespace dreal

#endif //CAV26_FILTER_SYMMETRIES
#endif //DREAL4_CMAKE_CAV26_VARNAME_PARSER_H
