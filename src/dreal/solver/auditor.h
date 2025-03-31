//
// Created by Kunal Sheth on 3/30/25.
//

#ifndef AUDITOR_H
#define AUDITOR_H

#include <dreal/util/box.h>

namespace dreal
{
    void audit(const Formula& formula, const std::optional<Box>& box);
}

#endif //AUDITOR_H
