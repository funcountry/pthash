#pragma once

#include "util.hpp"
#include "bit_vector.hpp"
#include <cstdio> // For fprintf, stderr etc.

// Define PTHASH_LOG if not already defined
#ifndef PTHASH_LOG
#ifdef PTHASH_ENABLE_INSTRUMENTATION
#define PTHASH_LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#define PTHASH_LOG(...) do {} while (0)
#endif
#endif

// Inline definition of instrumentation_context
namespace instrumentation_context {
    inline thread_local const char* current_prefix = nullptr;

    struct prefix_setter {
        prefix_setter(const char* prefix) { current_prefix = prefix; }
        ~prefix_setter() { current_prefix = nullptr; }
    };

    inline const char* get_prefix(const char* default_prefix = "[DEFAULT]") {
        return current_prefix ? current_prefix : default_prefix;
    }
}

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
        // *** START VANILLA INSTRUMENTATION ***
        const char* log_prefix = "[V_SELECT]";
        fprintf(stderr, "%s ENTER select(i=%llu)\n", log_prefix, (unsigned long long)i);
        // *** END VANILLA INSTRUMENTATION ***

        assert(i < num_positions());
        uint64_t block = i / block_size;
        // *** START VANILLA INSTRUMENTATION ***
        fprintf(stderr, "%s  Calculated block = %llu (i=%llu / block_size=%llu)\n", log_prefix, (unsigned long long)block, (unsigned long long)i, (unsigned long long)block_size);
        if (block >= m_block_inventory.size()) {
            fprintf(stderr, "%s  ERROR: block index %llu out of bounds (inventory size %zu)\n", log_prefix, (unsigned long long)block, m_block_inventory.size());
            fprintf(stderr, "%s EXIT select (error) -> returning 0\n", log_prefix);
            return 0; // Or appropriate error handling
        }
        // *** END VANILLA INSTRUMENTATION ***

        int64_t block_pos = m_block_inventory[block];
        // *** START VANILLA INSTRUMENTATION ***
        fprintf(stderr, "%s  Looked up block_pos = m_block_inventory[%llu] = %lld\n", log_prefix, (unsigned long long)block, (long long)block_pos);
        // *** END VANILLA INSTRUMENTATION ***

        if (block_pos < 0) {  // sparse super-block
            // *** START VANILLA INSTRUMENTATION ***
            fprintf(stderr, "%s  Block is SPARSE (block_pos < 0)\n", log_prefix);
            // *** END VANILLA INSTRUMENTATION ***
            uint64_t overflow_pos = uint64_t(-block_pos - 1);
            uint64_t index_in_block = i & (block_size - 1);
            uint64_t final_overflow_idx = overflow_pos + index_in_block;
            // *** START VANILLA INSTRUMENTATION ***
            fprintf(stderr, "%s    Calculated overflow_pos = %llu\n", log_prefix, (unsigned long long)overflow_pos);
            fprintf(stderr, "%s    Calculated index_in_block = %llu (i & (block_size - 1))\n", log_prefix, (unsigned long long)index_in_block);
            fprintf(stderr, "%s    Target overflow index = %llu\n", log_prefix, (unsigned long long)final_overflow_idx);
            if (final_overflow_idx >= m_overflow_positions.size()) {
                 fprintf(stderr, "%s    ERROR: final overflow index %llu out of bounds (overflow size %zu)\n", log_prefix, (unsigned long long)final_overflow_idx, m_overflow_positions.size());
                 fprintf(stderr, "%s EXIT select (sparse error) -> returning 0\n", log_prefix);
                 return 0; 
            }
            // *** END VANILLA INSTRUMENTATION ***
            uint64_t result = m_overflow_positions[final_overflow_idx];
            // *** START VANILLA INSTRUMENTATION ***
            fprintf(stderr, "%s    Result from m_overflow_positions[%llu] = %llu\n", log_prefix, (unsigned long long)final_overflow_idx, (unsigned long long)result);
            fprintf(stderr, "%s EXIT select (sparse) -> %llu\n", log_prefix, (unsigned long long)result);
            // *** END VANILLA INSTRUMENTATION ***
            return result;
        }

        // *** START VANILLA INSTRUMENTATION ***
        fprintf(stderr, "%s  Block is DENSE (block_pos >= 0)\n", log_prefix);
        // *** END VANILLA INSTRUMENTATION ***
        uint64_t subblock = i / subblock_size;
        // *** START VANILLA INSTRUMENTATION ***
        fprintf(stderr, "%s    Calculated subblock = %llu (i=%llu / subblock_size=%llu)\n", log_prefix, (unsigned long long)subblock, (unsigned long long)i, (unsigned long long)subblock_size);
        if (subblock >= m_subblock_inventory.size()) {
            fprintf(stderr, "%s    ERROR: subblock index %llu out of bounds (inventory size %zu)\n", log_prefix, (unsigned long long)subblock, m_subblock_inventory.size());
            fprintf(stderr, "%s EXIT select (error) -> returning 0\n", log_prefix);
            return 0; // Or appropriate error handling
        }
        // *** END VANILLA INSTRUMENTATION ***

        uint64_t start_pos = uint64_t(block_pos) + m_subblock_inventory[subblock];
        // *** START VANILLA INSTRUMENTATION ***
        fprintf(stderr, "%s    Looked up subblock offset = m_subblock_inventory[%llu] = %hu\n", log_prefix, (unsigned long long)subblock, m_subblock_inventory[subblock]);
        fprintf(stderr, "%s    Calculated start_pos = %llu (block_pos %llu + offset %hu)\n", log_prefix, (unsigned long long)start_pos, (unsigned long long)block_pos, m_subblock_inventory[subblock]);
        // *** END VANILLA INSTRUMENTATION ***

        uint64_t reminder = i & (subblock_size - 1);
        // *** START VANILLA INSTRUMENTATION ***
        fprintf(stderr, "%s    Calculated reminder = %llu (i & (subblock_size - 1))\n", log_prefix, (unsigned long long)reminder);
        // *** END VANILLA INSTRUMENTATION ***

        if (!reminder) {
            // *** START VANILLA INSTRUMENTATION ***
            fprintf(stderr, "%s    Reminder is 0, returning start_pos directly.\n", log_prefix);
            fprintf(stderr, "%s EXIT select (dense, reminder=0) -> %llu\n", log_prefix, (unsigned long long)start_pos);
            // *** END VANILLA INSTRUMENTATION ***
            return start_pos;
        }

        // *** START VANILLA INSTRUMENTATION ***
        fprintf(stderr, "%s    Reminder > 0, scanning forward from start_pos %llu\n", log_prefix, (unsigned long long)start_pos);
        // *** END VANILLA INSTRUMENTATION ***
        std::vector<uint64_t> const& data = B.data();
        uint64_t word_idx = start_pos >> 6;
        uint64_t word_shift = start_pos & 63;
        // *** START VANILLA INSTRUMENTATION ***
        fprintf(stderr, "%s      Initial word_idx = %llu, word_shift = %llu\n", log_prefix, (unsigned long long)word_idx, (unsigned long long)word_shift);
        if (word_idx >= data.size()) {
            fprintf(stderr, "%s      ERROR: initial word_idx %llu out of bounds (data size %zu)\n", log_prefix, (unsigned long long)word_idx, data.size());
            fprintf(stderr, "%s EXIT select (error) -> returning 0\n", log_prefix);
            return 0;
        }
        // *** END VANILLA INSTRUMENTATION ***

        uint64_t word = WordGetter()(data, word_idx) & (uint64_t(-1) << word_shift);
        // *** START VANILLA INSTRUMENTATION ***
        uint64_t original_first_word = WordGetter()(data, word_idx);
        fprintf(stderr, "%s      First word raw (data[%llu]) = 0x%llX\n", log_prefix, (unsigned long long)word_idx, (unsigned long long)original_first_word);
        fprintf(stderr, "%s      First word masked (>> %llu) = 0x%llX\n", log_prefix, (unsigned long long)word_shift, (unsigned long long)word);
        uint64_t loop_count = 0;
        // *** END VANILLA INSTRUMENTATION ***

        while (true) {
            // *** START VANILLA INSTRUMENTATION ***
            loop_count++;
            if (loop_count > (B.num_bits() / 64 + 2)) { // Safety break
                 fprintf(stderr, "%s      ERROR: Scan loop exceeded safety limit (%llu iterations)!\n", log_prefix, (unsigned long long)loop_count);
                 fprintf(stderr, "%s EXIT select (error) -> returning 0\n", log_prefix);
                 return 0;
            }
            // *** END VANILLA INSTRUMENTATION ***
            uint64_t popcnt = util::popcount(word);
            // *** START VANILLA INSTRUMENTATION ***
            fprintf(stderr, "%s      Loop %llu: word=0x%llX, popcnt=%llu, current reminder=%llu\n", log_prefix, (unsigned long long)loop_count, (unsigned long long)word, (unsigned long long)popcnt, (unsigned long long)reminder);
            // *** END VANILLA INSTRUMENTATION ***
            if (reminder < popcnt) {
                 // *** START VANILLA INSTRUMENTATION ***
                 fprintf(stderr, "%s        Reminder %llu < popcnt %llu, break loop.\n", log_prefix, (unsigned long long)reminder, (unsigned long long)popcnt);
                 // *** END VANILLA INSTRUMENTATION ***
                 break;
            }
            reminder -= popcnt;
            word_idx++;
            // *** START VANILLA INSTRUMENTATION ***
            fprintf(stderr, "%s        Reminder >= popcnt, reminder becomes %llu. Increment word_idx to %llu.\n", log_prefix, (unsigned long long)reminder, (unsigned long long)word_idx);
            if (word_idx >= data.size()) {
                fprintf(stderr, "%s        ERROR: next word_idx %llu out of bounds (data size %zu)!\n", log_prefix, (unsigned long long)word_idx, data.size());
                fprintf(stderr, "%s EXIT select (error) -> returning 0\n", log_prefix);
                return 0;
            }
            // *** END VANILLA INSTRUMENTATION ***
            word = WordGetter()(data, word_idx);
        }

        // *** START VANILLA INSTRUMENTATION ***
        fprintf(stderr, "%s      Loop finished. Final word_idx=%llu, word=0x%llX, reminder=%llu\n", log_prefix, (unsigned long long)word_idx, (unsigned long long)word, (unsigned long long)reminder);
        fprintf(stderr, "%s      Calling util::select_in_word(0x%llX, %llu)...\n", log_prefix, (unsigned long long)word, (unsigned long long)reminder);
        // *** END VANILLA INSTRUMENTATION ***
        uint64_t pos_in_word = util::select_in_word(word, reminder);
        uint64_t result = (word_idx << 6) + pos_in_word;
        // *** START VANILLA INSTRUMENTATION ***
        fprintf(stderr, "%s      select_in_word returned %llu\n", log_prefix, (unsigned long long)pos_in_word);
        fprintf(stderr, "%s EXIT select (dense) -> %llu ((word_idx %llu << 6) + pos_in_word %llu)\n", log_prefix, (unsigned long long)result, (unsigned long long)word_idx, (unsigned long long)pos_in_word);
        // *** END VANILLA INSTRUMENTATION ***
        return result;
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

    // *** START TEMPORARY PUBLIC GETTERS FOR TESTING ***
    public:
        const std::vector<int64_t>& getBlockInventory() const {
            return m_block_inventory;
        }
        const std::vector<uint16_t>& getSubblockInventory() const {
            return m_subblock_inventory;
        }
        const std::vector<uint64_t>& getOverflowPositions() const {
             return m_overflow_positions;
        }
        uint64_t getNumPositions() const {
            return m_positions;
        }
    // *** END TEMPORARY PUBLIC GETTERS FOR TESTING ***

protected:
    uint64_t m_positions;
    std::vector<int64_t> m_block_inventory;
    std::vector<uint16_t> m_subblock_inventory;
    std::vector<uint64_t> m_overflow_positions;

    template <typename Visitor, typename T>
    static void visit_impl(Visitor& visitor, T&& t) {
        // *** ADDED INSTRUMENTATION START ***
        // Use a context-dependent prefix set by the caller (elias_fano)
        const char* darray_prefix = instrumentation_context::get_prefix("[DARRAY_VISIT_UNKNOWN]");

        // Log m_positions
        PTHASH_LOG("%s.POSITIONS.BEFORE Value: %llu, Offset: %zu\n",
                darray_prefix, (unsigned long long)t.m_positions, (size_t)visitor.bytes());
        visitor.visit(t.m_positions);
        PTHASH_LOG("%s.POSITIONS.AFTER Offset: %zu\n",
                darray_prefix, (size_t)visitor.bytes());

        // Log m_block_inventory
        PTHASH_LOG("%s.BLOCK_INV.BEFORE Size: %lu, Offset: %zu\n",
                darray_prefix, (unsigned long)t.m_block_inventory.size(), (size_t)visitor.bytes());
#ifdef PTHASH_ENABLE_INSTRUMENTATION // Only loop if instrumentation is on
        for(size_t i = 0; i < t.m_block_inventory.size(); ++i) {
            // Log as signed int64_t as per the type definition
            PTHASH_LOG("%s.BLOCK_INV_DATA[%zu]=%lld\n",
                    darray_prefix, i, (long long)t.m_block_inventory[i]);
        }
#endif
        visitor.visit(t.m_block_inventory); // This will log size + data via vector visitor
        PTHASH_LOG("%s.BLOCK_INV.AFTER Offset: %zu\n",
                darray_prefix, (size_t)visitor.bytes());


        // Log m_subblock_inventory
        PTHASH_LOG("%s.SUBBLOCK_INV.BEFORE Size: %lu, Offset: %zu\n",
                darray_prefix, (unsigned long)t.m_subblock_inventory.size(), (size_t)visitor.bytes());
#ifdef PTHASH_ENABLE_INSTRUMENTATION // Only loop if instrumentation is on
        for(size_t i = 0; i < t.m_subblock_inventory.size(); ++i) {
            // Log as unsigned uint16_t
            PTHASH_LOG("%s.SUBBLOCK_INV_DATA[%zu]=%hu\n",
                    darray_prefix, i, (unsigned short)t.m_subblock_inventory[i]);
        }
#endif
        visitor.visit(t.m_subblock_inventory); // This will log size + data via vector visitor
        PTHASH_LOG("%s.SUBBLOCK_INV.AFTER Offset: %zu\n",
                darray_prefix, (size_t)visitor.bytes());

        // Log m_overflow_positions
        PTHASH_LOG("%s.OVERFLOW_POS.BEFORE Size: %lu, Offset: %zu\n",
                darray_prefix, (unsigned long)t.m_overflow_positions.size(), (size_t)visitor.bytes());
#ifdef PTHASH_ENABLE_INSTRUMENTATION // Only loop if instrumentation is on
        for(size_t i = 0; i < t.m_overflow_positions.size(); ++i) {
            // Log as unsigned uint64_t
            PTHASH_LOG("%s.OVERFLOW_POS_DATA[%zu]=%llu\n",
                    darray_prefix, i, (unsigned long long)t.m_overflow_positions[i]);
        }
#endif
        visitor.visit(t.m_overflow_positions); // This will log size + data via vector visitor
        PTHASH_LOG("%s.OVERFLOW_POS.AFTER Offset: %zu\n",
                darray_prefix, (size_t)visitor.bytes());

        // *** ADDED INSTRUMENTATION END ***

        /* Original code was likely just this:
        visitor.visit(t.m_positions);
        visitor.visit(t.m_block_inventory);
        visitor.visit(t.m_subblock_inventory);
        visitor.visit(t.m_overflow_positions);
        */
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