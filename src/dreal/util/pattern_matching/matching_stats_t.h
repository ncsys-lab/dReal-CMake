//
// Created by Kunal Sheth on 12/27/25.
//

#ifndef DREAL4_CMAKE_MATCHING_STATS_T_H
#define DREAL4_CMAKE_MATCHING_STATS_T_H
#include "substitutions_map.h"
#include <array>

namespace dreal
{
    using matching_stats_t = struct
    {
        std::array<unsigned, substitutions_map::LEN_substitution_statuses> misses_bc;
        unsigned partial_matches;
        unsigned matches;
    };
} // namespace dreal

#endif //DREAL4_CMAKE_MATCHING_STATS_T_H
