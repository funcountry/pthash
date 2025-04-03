#pragma once

#include "util.hpp"
#include "bit_vector.hpp"

namespace bits {

/*
    The following class implements an index on top of an uncompressed
    bitvector B[0..n) to support select queries.

    The solution implemented here is described in

        Okanohara, Daisuke, and Kunihiko Sadakane. 2007.
        Practical entropy-compressed rank/select dictionary.
        In Proceedings of the 9-th Workshop on Algorithm Engineering
        and Experiments (ALENEX), pages 60-70.

    under the name "darray" (short for "dense" array).

    The bitvector is split into variable-length super-blocks, each
    containing L ones (except for, possibly, the last super-block).
    A super-block is said to be "sparse" if its length is >= L2;
    otherwise it is said "dense". Sparse super-blocks are represented
    verbatim, i.e., the positions of the L ones are coded using 64-bit
    integers. A dense super-block, instead, is sparsified: we keep
    one position every L3 positions. The positions are coded relatively
    to the beginning of each super-block, hence using log2(L2) bits per
    position.

    A select query first checks if the super-block is sparse: if it is,
    then the query is solved in O(1). If the super-block is dense instead,
    the corresponding block is accessed and a linear scan is performed
    for a worst-case cost of O(L2/L3).

    This implementation uses, by default:

    L =  1,024 (block_size)
    L2 = 65,536 (so that each position in a dense block can be coded
                 using 16-bit integers)
    L3 = 32 (subblock_size)
*/

template <                       //
    typename WordGetter,         //
    uint64_t block_size = 1024,  //
    uint64_t subblock_size = 32  //
    >
struct darray {
    darray() : m_positions(0) {}

    void build(bit_vector const& B) {
        std::vector<uint64_t> const& data = B.data();
        std::vector<uint64_t> cur_block_positions;
        std::vector<int64_t> block_inventory;
        std::vector<uint16_t> subblock_inventory;
        std::vector<uint64_t> overflow_positions;

        for (uint64_t word_idx = 0; word_idx < data.size(); ++word_idx) {
            uint64_t cur_pos = word_idx << 6;
            uint64_t cur_word = WordGetter()(data, word_idx);
            uint64_t l;
            while (util::lsbll(cur_word, l)) {
                cur_pos += l;
                cur_word >>= l;
                if (cur_pos >= B.num_bits()) break;

                cur_block_positions.push_back(cur_pos);

                if (cur_block_positions.size() == block_size) {
                    flush_cur_block(cur_block_positions, block_inventory, subblock_inventory,
                                    overflow_positions);
                }

                // can't do >>= l + 1, can be 64
                cur_word >>= 1;
                cur_pos += 1;
                m_positions += 1;
            }
        }
        if (cur_block_positions.size()) {
            flush_cur_block(cur_block_positions, block_inventory, subblock_inventory,
                            overflow_positions);
        }
        m_block_inventory.swap(block_inventory);
        m_subblock_inventory.swap(subblock_inventory);
        m_overflow_positions.swap(overflow_positions);
    }

    /*
        Return the position of the i-th bit set in B,
        for any 0 <= i < num_positions();
    */
    inline uint64_t select(bit_vector const& B, uint64_t i) const {
        fprintf(stderr, "%s ENTER select(i=%llu)\n", P8_LOG_PREFIX, (unsigned long long)i);
        assert(i < num_positions());
        
        uint64_t block = i / block_size;
        int64_t block_pos = m_block_inventory[block];
        fprintf(stderr, "%s   block=%llu, block_pos=%lld\n", P8_LOG_PREFIX, (unsigned long long)block, (long long)block_pos);
        
        if (block_pos < 0) {  // sparse super-block
            uint64_t overflow_pos = uint64_t(-block_pos - 1);
            uint64_t idx = overflow_pos + (i & (block_size - 1));
            uint64_t result = m_overflow_positions[idx];
            fprintf(stderr, "%s   SPARSE BLOCK: overflow_pos=%llu, idx=%llu, result=%llu\n", 
                    P8_LOG_PREFIX, (unsigned long long)overflow_pos, (unsigned long long)idx, (unsigned long long)result);
            fprintf(stderr, "%s EXIT select -> %llu (sparse block)\n", P8_LOG_PREFIX, (unsigned long long)result);
            return result;
        }

        uint64_t subblock = i / subblock_size;
        uint64_t subblock_offset = m_subblock_inventory[subblock];
        uint64_t start_pos = uint64_t(block_pos) + subblock_offset;
        uint64_t reminder = i & (subblock_size - 1);
        
        fprintf(stderr, "%s   DENSE BLOCK: subblock=%llu, subblock_offset=%llu, start_pos=%llu, reminder=%llu\n", 
                P8_LOG_PREFIX, (unsigned long long)subblock, (unsigned long long)subblock_offset, 
                (unsigned long long)start_pos, (unsigned long long)reminder);
        
        if (!reminder) {
            fprintf(stderr, "%s EXIT select -> %llu (exact subblock)\n", P8_LOG_PREFIX, (unsigned long long)start_pos);
            return start_pos;
        }

        std::vector<uint64_t> const& data = B.data();
        uint64_t word_idx = start_pos >> 6;
        uint64_t word_shift = start_pos & 63;
        uint64_t word = WordGetter()(data, word_idx) & (uint64_t(-1) << word_shift);
        
        fprintf(stderr, "%s   SCANNING: word_idx=%llu, word_shift=%llu, initial_word=0x%llx\n", 
                P8_LOG_PREFIX, (unsigned long long)word_idx, (unsigned long long)word_shift, (unsigned long long)word);
        
        uint64_t popcnt_debug = 0;
        while (true) {
            uint64_t popcnt = util::popcount(word);
            popcnt_debug = popcnt;
            fprintf(stderr, "%s   SCANNING: word=0x%llx, popcnt=%llu, reminder=%llu\n", 
                    P8_LOG_PREFIX, (unsigned long long)word, (unsigned long long)popcnt, (unsigned long long)reminder);
            
            if (reminder < popcnt) break;
            reminder -= popcnt;
            word = WordGetter()(data, ++word_idx);
        }
        
        uint64_t select_in_word_result = util::select_in_word(word, reminder);
        uint64_t final_result = (word_idx << 6) + select_in_word_result;
        
        fprintf(stderr, "%s   FOUND: word_idx=%llu, popcnt=%llu, select_in_word=%llu, final=%llu\n", 
                P8_LOG_PREFIX, (unsigned long long)word_idx, (unsigned long long)popcnt_debug, 
                (unsigned long long)select_in_word_result, (unsigned long long)final_result);
        
        fprintf(stderr, "%s EXIT select -> %llu\n", P8_LOG_PREFIX, (unsigned long long)final_result);
        return final_result;
    }

    inline uint64_t num_positions() const { return m_positions; }

    uint64_t num_bytes() const {
        return sizeof(m_positions) + essentials::vec_bytes(m_block_inventory) +
               essentials::vec_bytes(m_subblock_inventory) +
               essentials::vec_bytes(m_overflow_positions);
    }

    void swap(darray& other) {
        std::swap(m_positions, other.m_positions);
        m_block_inventory.swap(other.m_block_inventory);
        m_subblock_inventory.swap(other.m_subblock_inventory);
        m_overflow_positions.swap(other.m_overflow_positions);
    }

    template <typename Visitor>
    void visit(Visitor& visitor) const {
        visit_impl(visitor, *this);
    }

    template <typename Visitor>
    void visit(Visitor& visitor) {
        visit_impl(visitor, *this);
    }

protected:
    uint64_t m_positions;
    std::vector<int64_t> m_block_inventory;
    std::vector<uint16_t> m_subblock_inventory;
    std::vector<uint64_t> m_overflow_positions;

    template <typename Visitor, typename T>
    static void visit_impl(Visitor& visitor, T&& t) {
        const char* prefix = "[P3.SAVE.DARRAY]";

        // Log m_positions
        size_t offset_before_positions = visitor.bytes();
        fprintf(stderr, "%s.BEFORE Name: %s, Type: %s, Size: %lu, Offset: %zu\n",
                prefix, "m_positions", "uint64_t", sizeof(t.m_positions), offset_before_positions);
        visitor.visit(t.m_positions);
        size_t offset_after_positions = visitor.bytes();
        fprintf(stderr, "%s.AFTER Name: %s, BytesWritten: %zu, FinalOffset: %zu\n",
                prefix, "m_positions", offset_after_positions - offset_before_positions, offset_after_positions);

        // Log m_block_inventory
        size_t offset_before_block_inv = visitor.bytes();
        fprintf(stderr, "%s.BEFORE Name: %s, Type: %s, Offset: %zu\n",
                prefix, "m_block_inventory", "std::vector<int64_t>", offset_before_block_inv);
        visitor.visit(t.m_block_inventory);
        size_t offset_after_block_inv = visitor.bytes();
        fprintf(stderr, "%s.AFTER Name: %s, BytesWritten: %zu, FinalOffset: %zu\n",
                prefix, "m_block_inventory", offset_after_block_inv - offset_before_block_inv, offset_after_block_inv);

        // Log m_subblock_inventory
        size_t offset_before_subblock_inv = visitor.bytes();
        fprintf(stderr, "%s.BEFORE Name: %s, Type: %s, Offset: %zu\n",
                prefix, "m_subblock_inventory", "std::vector<uint16_t>", offset_before_subblock_inv);
        visitor.visit(t.m_subblock_inventory);
        size_t offset_after_subblock_inv = visitor.bytes();
        fprintf(stderr, "%s.AFTER Name: %s, BytesWritten: %zu, FinalOffset: %zu\n",
                prefix, "m_subblock_inventory", offset_after_subblock_inv - offset_before_subblock_inv, offset_after_subblock_inv);

        // Log m_overflow_positions
        size_t offset_before_overflow = visitor.bytes();
        fprintf(stderr, "%s.BEFORE Name: %s, Type: %s, Offset: %zu\n",
                prefix, "m_overflow_positions", "std::vector<uint64_t>", offset_before_overflow);
        visitor.visit(t.m_overflow_positions);
        size_t offset_after_overflow = visitor.bytes();
        fprintf(stderr, "%s.AFTER Name: %s, BytesWritten: %zu, FinalOffset: %zu\n",
                prefix, "m_overflow_positions", offset_after_overflow - offset_before_overflow, offset_after_overflow);
    }

    static void flush_cur_block(std::vector<uint64_t>& cur_block_positions,
                                std::vector<int64_t>& block_inventory,
                                std::vector<uint16_t>& subblock_inventory,
                                std::vector<uint64_t>& overflow_positions) {
        if (cur_block_positions.back() - cur_block_positions.front() < (1ULL << 16))  // dense case
        {
            block_inventory.push_back(int64_t(cur_block_positions.front()));
            for (uint64_t i = 0; i < cur_block_positions.size(); i += subblock_size) {
                subblock_inventory.push_back(
                    uint16_t(cur_block_positions[i] - cur_block_positions.front()));
            }
        } else  // sparse case
        {
            block_inventory.push_back(-int64_t(overflow_positions.size()) - 1);
            for (uint64_t i = 0; i < cur_block_positions.size(); ++i) {
                overflow_positions.push_back(cur_block_positions[i]);
            }
            for (uint64_t i = 0; i < cur_block_positions.size(); i += subblock_size) {
                subblock_inventory.push_back(uint16_t(-1));
            }
        }
        cur_block_positions.clear();
    }

    // Use a detailed log prefix for debug output in Phase 8
    const char* P8_LOG_PREFIX = "[P8.D1_SELECT]";
};

namespace util {

struct identity_getter {
    uint64_t operator()(std::vector<uint64_t> const& data, uint64_t i) const { return data[i]; }
};

struct negating_getter {
    uint64_t operator()(std::vector<uint64_t> const& data, uint64_t i) const { return ~data[i]; }
};

}  // namespace util

typedef darray<util::identity_getter> darray1;  // take positions of 1s
typedef darray<util::negating_getter> darray0;  // take positions of 0s

}  // namespace bits