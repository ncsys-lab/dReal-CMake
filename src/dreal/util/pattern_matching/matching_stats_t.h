//
// Created by Kunal Sheth on 12/27/25.
//

#ifndef DREAL4_CMAKE_MATCHING_STATS_T_H
#define DREAL4_CMAKE_MATCHING_STATS_T_H

typedef struct
{
    struct
    {
        unsigned bc_structure;
        unsigned bc_indices;
        unsigned bc_type;
        unsigned bc_box;
        unsigned bc_bij;
        unsigned bc_const;
    } misses;

    unsigned partial_matches;
    unsigned matches;
} matching_stats_t;

#endif //DREAL4_CMAKE_MATCHING_STATS_T_H