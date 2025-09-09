#include "dreal/util/iterators.h"

#include <iostream>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "dreal/symbolic/symbolic.h"

using std::is_nothrow_move_constructible;
using std::numeric_limits;
using std::pair;
using std::vector;

namespace dreal
{
    namespace
    {
        class IteratorsTest : public ::testing::Test
        {};

        TEST_F(IteratorsTest, ConcatConstReferences) {
            const std::vector v123{1, 2, 3};
            const std::vector v456{4, 5, 6};
            auto c_view = concat_view(v123, v456);
            EXPECT_EQ(c_view.size(), 6);
            auto it = c_view.begin();
            EXPECT_EQ(*it, 1);
            EXPECT_EQ(*it, 1);
            ++it;
            EXPECT_EQ(*it, 2);
            ++it;++it;
            EXPECT_EQ(*it, 4);
            EXPECT_EQ(*++it, 5);
            EXPECT_EQ(*++it, 6);
            EXPECT_EQ(++it, c_view.end());
        }

        TEST_F(IteratorsTest, ConcatForeachNotation) {
            const std::string a("hot");
            const std::string b("cross");
            const std::string c("buns");
            std::string acc;
            for (const auto& x : concat_view(a, concat_view(b, c))) acc += x;
            EXPECT_EQ(acc, "hotcrossbuns");
        }
    } // namespace
} // namespace dreal
