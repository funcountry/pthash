#pragma once

#include "bit_vector.hpp"
#include "darray.hpp"
#include "compact_vector.hpp"
// instrumentation_context is now defined in darray.hpp

#include <iterator>
#include <cstdio> // For fprintf

namespace bits {

template <  //
    /* build a succinct select index on the zeros of high_bits for efficient next_neq */
    bool index_zeros = false,  //
    /* if encode_prefix_sum = false, the sequence is assumed to be sorted */
    bool encode_prefix_sum = false,  //

    typename DArray1 = darray1,  //
    typename DArray0 = darray0   //
    >
struct elias_fano {
    elias_fano() : m_back(0) {}

    template <typename Iterator>
    void encode(Iterator begin, uint64_t n, uint64_t universe = uint64_t(-1)) {
        if (n == 0) return;

        if constexpr (encode_prefix_sum) {
            universe = 0;
            auto tmp = begin;
            for (uint64_t i = 0; i != n; ++i, ++tmp) universe += *tmp;
            n = n + 1;  // because a zero is added at the beginning
        } else {
            if (universe == uint64_t(-1))  // otherwise use the provided universe
            {
                if constexpr (std::is_same_v<typename Iterator::iterator_category,
                                             std::random_access_iterator_tag>) {
                    universe = *(begin + (n - 1));
                } else {  // scan
                    auto tmp = begin;
                    for (uint64_t i = 0; i != n - 1; ++i, ++tmp)
                        ;
                    universe = *tmp;
                }
            }
        }

        /* This version takes at most: n*floor(log(U/n)) + 3*n bits */
        uint64_t l = uint64_t((n && universe / n) ? util::msb(universe / n) : 0);

        /* This version takes at most: n*ceil(log(U/n)) + 2*n bits */
        // uint64_t l = std::ceil(std::log2(static_cast<double>(universe) / n));

        /*
            Q. Which version is better?
            A. It depends on the indexes built on the high_bits.
        */

        bit_vector::builder bvb_high_bits(n + (universe >> l) + 1);
        compact_vector::builder cvb_low_bits(n, l);

        const uint64_t low_mask = (uint64_t(1) << l) - 1;
        uint64_t last = 0;

        // add a zero at the beginning
        if constexpr (encode_prefix_sum) {
            if (l) cvb_low_bits.set(0, 0);
            bvb_high_bits.set(0, 1);
            n = n - 1;  // restore n
        }

        for (uint64_t i = 0; i != n; ++i, ++begin) {
            auto v = *begin;
            if constexpr (encode_prefix_sum) {
                v = v + last;             // prefix sum
            } else if (i and v < last) {  // check the order
#if PTHASH_ENABLE_INSTRUMENTATION
                std::cerr << "error at " << i << "/" << n << ":\n";
                std::cerr << "last " << last << "\n";
                std::cerr << "current " << v << "\n";
#endif
                throw std::runtime_error("sequence is not sorted");
            }
            if (l) cvb_low_bits.set(i + encode_prefix_sum, v & low_mask);
            bvb_high_bits.set((v >> l) + i + encode_prefix_sum, 1);
            last = v;
        }

        m_back = last;
        bvb_high_bits.build(m_high_bits);
        cvb_low_bits.build(m_low_bits);
        m_high_bits_d1.build(m_high_bits);
        if constexpr (index_zeros) m_high_bits_d0.build(m_high_bits);
    }

    struct iterator {
        iterator() : m_ef(nullptr), m_pos(0), m_l(0), m_val(0) {}

        iterator(elias_fano const* ef, uint64_t pos = 0)
            : m_ef(ef)
            , m_pos(pos)
            , m_l(ef->m_low_bits.width())
            , m_val(0)  //
        {
            if (!has_next() or m_ef->m_high_bits_d1.num_positions() == 0) return;
            assert(m_l < 64);
            uint64_t begin = m_ef->m_high_bits_d1.select(m_ef->m_high_bits, m_pos);
            m_high_bits_it = m_ef->m_high_bits.get_iterator_at(begin);
            m_low_bits_it = m_ef->m_low_bits.get_iterator_at(m_pos);
            read_next_value();
        }

        bool has_next() const { return m_pos < m_ef->size(); }
        bool has_prev() const { return m_pos > 0; }
        uint64_t value() const { return m_val; }
        uint64_t position() const { return m_pos; }

        void next() {
            ++m_pos;
            if (!has_next()) return;
            read_next_value();
        }

        /*
            Return the value before the current position.
        */
        uint64_t prev_value() {
            assert(m_pos > 0);
            uint64_t pos = m_pos - 1;
            /*
                Read_next_value() sets the state ahead of 1 position,
                hence must go back by 2 to get previous value.
            */
            assert(m_high_bits_it.position() >= 2);
            uint64_t high = m_high_bits_it.prev(m_high_bits_it.position() - 2);
            assert(high == m_ef->m_high_bits_d1.select(m_ef->m_high_bits, pos));
            uint64_t low = *(m_low_bits_it - 2);
            return (((high - pos) << m_l) | low);
        }

    private:
        elias_fano const* m_ef;
        uint64_t m_pos;
        uint64_t m_l;
        uint64_t m_val;
        bit_vector::iterator m_high_bits_it;
        compact_vector::iterator m_low_bits_it;

        void read_next_value() {
            assert(m_pos < m_ef->size());
            uint64_t high = m_high_bits_it.next();
            assert(high == m_ef->m_high_bits_d1.select(m_ef->m_high_bits, m_pos));
            uint64_t low = *m_low_bits_it;
            m_val = (((high - m_pos) << m_l) | low);
            ++m_low_bits_it;
        }
    };

    iterator get_iterator_at(uint64_t pos) const { return iterator(this, pos); }
    iterator begin() const { return get_iterator_at(0); }

    uint64_t access(uint64_t i) const {
        PTHASH_LOG("[P8.EF_ACCESS] ENTER access(i=%llu)\n", (unsigned long long)i);
        assert(i < size());
        PTHASH_LOG("[P8.EF_ACCESS]   Calling m_high_bits_d1.select(m_high_bits, i=%llu)...\n", (unsigned long long)i);
        uint64_t select_result = m_high_bits_d1.select(m_high_bits, i);
        PTHASH_LOG("[P8.EF_ACCESS]   m_high_bits_d1.select returned: %llu\n", (unsigned long long)select_result);
        uint64_t high_val = select_result - i;
        PTHASH_LOG("[P8.EF_ACCESS]   Calculated high_val = select_result - i = %llu\n", (unsigned long long)high_val);
        uint64_t width = m_low_bits.width();
        PTHASH_LOG("[P8.EF_ACCESS]   m_low_bits.width() = %llu\n", (unsigned long long)width);
        PTHASH_LOG("[P8.EF_ACCESS]   Calling m_low_bits.access(i=%llu)...\n", (unsigned long long)i);
        uint64_t low_val = m_low_bits.access(i);
        PTHASH_LOG("[P8.EF_ACCESS]   m_low_bits.access returned: %llu\n", (unsigned long long)low_val);
        uint64_t final_pos = (high_val << width) | low_val;
        PTHASH_LOG("[P8.EF_ACCESS] EXIT access -> (high_val << width) | low_val = (%llu << %llu) | %llu = %llu\n",
                (unsigned long long)high_val, (unsigned long long)width, (unsigned long long)low_val, (unsigned long long)final_pos);
        return final_pos;
    }

    /*
        If encode_prefix_sum = true, the iterator passed to the encode() method
        can yield non-sorted values, so that diff(i) returns the i-th element
        from the original sequence.
        Example. Assume the values are V = [3, 2, 5, 1, 16],
        then we will encode V' = [0, 3, 5, 10, 11, 27], so that
            diff(0) = V[0] = V'[1] - V'[0] = 3-0 = 3
            diff(1) = V[1] = V'[2] - V'[1] = 5-1 = 2
            diff(2) = V[2] = V'[3] - V'[2] = 10-5 = 5
            diff(3) = V[3] = V'[4] - V'[3] = 11-10 = 1
            diff(4) = V[4] = V'[5] - V'[4] = 27-11 = 16
    */
    uint64_t diff(uint64_t i) const {
        assert(i < size() && encode_prefix_sum);
        uint64_t low1 = m_low_bits.access(i);
        uint64_t low2 = m_low_bits.access(i + 1);
        uint64_t l = m_low_bits.width();
        uint64_t pos = m_high_bits_d1.select(m_high_bits, i);
        uint64_t h1 = pos - i;
        uint64_t h2 = m_high_bits.get_iterator_at(pos + 1).next() - i - 1;
        uint64_t val1 = (h1 << l) | low1;
        uint64_t val2 = (h2 << l) | low2;
        return val2 - val1;
    }

    struct return_value {
        uint64_t pos;
        uint64_t val;
    };

    /*
        Return [position,value] of the leftmost smallest element that is >= x.
        Return [size()-1,back()] if x > back().

        Example.

        1, 3, 3, 4, 5, 6, 6, 9, 12, 14, 17, 17
        0  1  2  3  4  5  6  7   8   9  10  11

        next_geq(0) = [0,1]
        next_geq(3) = [1,3]
        next_geq(6) = [5,6]
        next_geq(7) = [7,9]
        next_geq(17) = [10,17]
        next_geq(23) = [11,17] (saturate)
    */
    return_value next_geq(const uint64_t x) const { return next_geq_leftmost(x).first; }

    /*
        Return [position,value] of the rightmost largest element that is <= x.
        Return [size()-1,back()] if x >= back().
        Return [uint64(-1),uint64(-1)] if x < front()
        (result is undefined; uint64(-1) = 2^64-1).

        Example.

        1, 3, 3, 4, 5, 6, 6, 9, 12, 14, 17, 17
        0  1  2  3  4  5  6  7   8   9  10  11

        prev_leq(0) = [uint64(-1),uint64(-1)] (undefined, because 0 < front() = 1)
        prev_leq(3) = [2,3]
        prev_leq(6) = [6,6]
        prev_leq(7) = [6,6]
        prev_leq(17) = [11,17]
        prev_leq(23) = [11,17] (saturate, because 23 >= back() = 17)
    */
    return_value prev_leq(const uint64_t x) const {
        auto [ret, it] = next_geq_rightmost(x);
        if (ret.val > x) return {ret.pos - 1, ret.pos != 0 ? it.prev_value() : uint64_t(-1)};
        return ret;
    }

    /*
        Determine integers lo and hi, with lo < hi, such that lo <= x < hi
        and lo is the largest rightmost value that is <= x (hence, it is prev_leq(x))
        and hi is the smallest leftmost value that is > x.

        Return the tuple [lo_pos, lo, hi_pos, hi].

        Return [position,value] of the rightmost largest element that is <= x.
        Return [size()-1,back()] if x >= back().
        Return [uint64(-1),uint64(-1)] if x < front() (result is undefined;
        uint64(-1) = 2^64-1).

        Example.

        1, 3, 3, 4, 5, 6, 6, 9, 12, 14, 17, 17
        0  1  2  3  4  5  6  7   8   9  10  11

        locate(0) = [uint64(-1),uint64(-1), 0, 1]
        locate(3) = [2,3,3,4]
        locate(6) = [6,6,7,9]
        locate(7) = [6,6,7,9]
        locate(17) = [11,17,11,17]
        locate(23) = [11,17,uint64(-1),uint64(-1)] (saturate, because 23 >= back() = 17)
    */
    std::pair<return_value, return_value> locate(const uint64_t x) const {
        auto [lo, it] = next_geq_rightmost(x);
        if (lo.val > x) {
            lo.val = lo.pos != 0 ? it.prev_value() : uint64_t(-1);
            lo.pos -= 1;
        }
        return_value hi{uint64_t(-1), uint64_t(-1)};
        if (lo.pos != size() - 1) {
            hi.pos = lo.pos + 1;
            hi.val = it.value();  // element next to lo.val
            assert(it.position() == hi.pos);
        }
        return {lo, hi};
    }

    uint64_t back() const { return m_back; }
    uint64_t size() const { return m_low_bits.size(); }

    auto num_low_bits() const {
        return m_low_bits.num_bytes() * 8;
    }

    uint64_t num_bytes() const {
        return sizeof(m_back) + m_high_bits.num_bytes() + m_high_bits_d1.num_bytes() +
               m_high_bits_d0.num_bytes() + m_low_bits.num_bytes();
    }

    // ========= START AGGRESSIVE GETTERS =========
    uint64_t get_back() const {
        return m_back;
    }

    const bits::bit_vector& get_high_bits() const {
        return m_high_bits;
    }

    const bits::compact_vector& get_low_bits() const {
        return m_low_bits;
    }
    
    // Add DArray getters
    const DArray1& get_high_bits_d1() const {
        return m_high_bits_d1;
    }
    
    const DArray0& get_high_bits_d0() const {
        return m_high_bits_d0;
    }
    // ========= END AGGRESSIVE GETTERS =========

    void swap(elias_fano& other) {
        std::swap(m_back, other.m_back);
        m_high_bits.swap(other.m_high_bits);
        m_high_bits_d1.swap(other.m_high_bits_d1);
        m_high_bits_d0.swap(other.m_high_bits_d0);
        m_low_bits.swap(other.m_low_bits);
    }

    template <typename Visitor>
    void visit(Visitor& visitor) const {
        visit_impl(visitor, *this);
    }

    template <typename Visitor>
    void visit(Visitor& visitor) {
        visit_impl(visitor, *this);
    }

private:
    uint64_t m_back;
    bit_vector m_high_bits;
    DArray1 m_high_bits_d1;
    DArray0 m_high_bits_d0;
    compact_vector m_low_bits;

    template <typename Visitor, typename T>
    static void visit_impl(Visitor& visitor, T&& t) {
        //PTHASH_LOG("[P3.EF] ENTER elias_fano::visit_impl\n");
        visitor.visit(t.m_back);
        visitor.visit(t.m_high_bits);

        // *** ADDED CONTEXT LOGGING FOR DARRAY1 ***
        PTHASH_LOG("[EF_SAVE] Visiting m_high_bits_d1 START\n");
        { // Scope for prefix setter
            instrumentation_context::prefix_setter ctx("[DARRAY1_SAVE]");
            visitor.visit(t.m_high_bits_d1);
        }
        PTHASH_LOG("[EF_SAVE] Visiting m_high_bits_d1 END\n");
        // *** END CONTEXT LOGGING ***

        // *** ADDED CONTEXT LOGGING FOR DARRAY0 ***
        PTHASH_LOG("[EF_SAVE] Visiting m_high_bits_d0 START\n");
        { // Scope for prefix setter
            instrumentation_context::prefix_setter ctx("[DARRAY0_SAVE]");
            visitor.visit(t.m_high_bits_d0);
        }
        PTHASH_LOG("[EF_SAVE] Visiting m_high_bits_d0 END\n");
        // *** END CONTEXT LOGGING ***

        // Log m_low_bits (as before)
        PTHASH_LOG("[CV_LOWBITS_SAVE] Pre-Save State: Size=%llu, Width=%llu, Mask=0x%llX, DataSize=%lu\n",
            (unsigned long long)t.m_low_bits.size(),
            (unsigned long long)t.m_low_bits.width(),
            (unsigned long long)(t.m_low_bits.width() == 64 ? uint64_t(-1) : ((uint64_t(1) << t.m_low_bits.width()) - 1)),
            (unsigned long)t.m_low_bits.data().size());
        visitor.visit(t.m_low_bits);
        PTHASH_LOG("[CV_LOWBITS_SAVE] Finished visiting m_low_bits\n");
        //PTHASH_LOG("[P3.EF] EXIT elias_fano::visit_impl\n");
    }

    /*
        Return [position,value] of the leftmost smallest element that is >= x.
        Return [size()-1,back()] if x > back().
    */
    std::pair<return_value, iterator> next_geq_leftmost(const uint64_t x) const {
        static_assert(index_zeros == true, "must build index on zeros");
        assert(m_high_bits_d0.num_positions());

        if (x > back()) return {{size() - 1, back()}, iterator()};

        uint64_t h_x = x >> m_low_bits.width();
        uint64_t begin = h_x ? m_high_bits_d0.select(m_high_bits, h_x - 1) - h_x + 1 : 0;
        assert(begin < size());

        // uint64_t end = m_high_bits_d0.select(m_high_bits, h_x) - h_x;
        // assert(end <= size());
        // assert(begin <= end);
        // return binary search for x in [begin, end)

        auto it = get_iterator_at(begin);
        uint64_t pos = begin;
        uint64_t val = it.value();
        while (val < x) {
            ++pos;
            /*
                Note: no need for bound checking here
                because x <= back(), hence pos cannot
                be equal to size().
            */
            it.next();
            val = it.value();
        }
        /* now pos is the position of the leftmost element that is >= x */
        assert(val >= x);
        assert(pos < size());
        assert(val == access(pos));
        assert(it.position() == pos);
        return {{pos, val}, it};
    }

    /*
        Return [position,value] of the rightmost smallest element that is >= x.
        Return [size()-1,back()] if x >= back().
    */
    std::pair<return_value, iterator> next_geq_rightmost(const uint64_t x) const {
        auto [ret, it] = next_geq_leftmost(x);
        if (ret.val == x and ret.pos != size() - 1) {
            assert(it.position() == ret.pos);
            do {  // scan to pick the rightmost one
                ++ret.pos;
                if (ret.pos == size()) break;
                it.next();
                ret.val = it.value();
            } while (ret.val == x);
            assert(ret.val >= x);
            assert(ret.pos > 0);
            ret.pos -= 1;
            ret.val = x;
        }
        return {ret, it};
    }
};

}  // namespace bits