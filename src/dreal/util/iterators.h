//
// Created by Kunal Sheth on 8/28/25.
//

#ifndef DREAL4_CMAKE_ITERATORS_H
#define DREAL4_CMAKE_ITERATORS_H

#include <iterator>
// #include <type_traits>

template <typename C1, typename C2>
class concat_view
{
public:
    concat_view(const C1& c1, const C2& c2) : c1_(c1), c2_(c2) {}

    class iterator
    {
    public:
        using it1_t = decltype(std::declval<C1&>().cbegin());
        using it2_t = decltype(std::declval<C2&>().cbegin());
        // using value_type = std::common_type_t<
        // typename std::iterator_traits<it1_t>::value_type,
        // typename std::iterator_traits<it2_t>::value_type>;
        // using difference_type = std::ptrdiff_t;
        // using reference = value_type&;
        // using pointer = value_type*;
        // using iterator_category = std::forward_iterator_tag;

        iterator(it1_t it1, it1_t end1, it2_t it2, it2_t end2, bool in_second)
            : in_second_(in_second), it1_(it1), it2_(it2), end1_(end1), end2_(end2) {
            if (!in_second_ && it1_ == end1_) in_second_ = true;
        }

        auto& operator*() const { return in_second_ ? *it2_ : *it1_; }

        iterator& operator++() {
            if (!in_second_) {
                ++it1_;
                if (it1_ == end1_) in_second_ = true;
            }
            else ++it2_;
            return *this;
        }

        bool operator==(const iterator& o) const {
            if (in_second_ != o.in_second_) { // edge case: all-empty
                return it1_ == end1_ && it2_ == end2_ &&
                    o.it1_ == o.end1_ && o.it2_ == o.end2_;
            }
            return in_second_ ? it2_ == o.it2_ : it1_ == o.it1_;
        }

        bool operator!=(const iterator& o) const { return !(*this == o); }

    private:
        bool in_second_;
        it1_t it1_;
        it2_t it2_;
        const it1_t end1_;
        const it2_t end2_;
    };

    iterator begin() const { return iterator(c1_.cbegin(), c1_.cend(), c2_.cbegin(), c2_.cend(), false); }
    iterator end() const { return iterator(c1_.cend(), c1_.cend(), c2_.cend(), c2_.cend(), true); }
    iterator cbegin() const { return begin(); }
    iterator cend() const { return end(); }

    size_t size() const { return c1_.size() + c2_.size(); }

private:
    const C1& c1_;
    const C2& c2_;
};

#endif //DREAL4_CMAKE_ITERATORS_H
