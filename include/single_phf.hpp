#pragma once

#include <cstdio> // For fprintf
#include "utils/bucketers.hpp"
#include "builders/util.hpp"
#include "builders/internal_memory_builder_single_phf.hpp"
#include "builders/external_memory_builder_single_phf.hpp"

namespace pthash {

template <typename Hasher,    //
          typename Bucketer,  //
          typename Encoder,   //
          bool Minimal,
          pthash_search_type Search>
struct single_phf  //
{
    static_assert(
        !std::is_base_of<dense_encoder, Encoder>::value,
        "Dense encoders are only valid for dense_partitioned_phf. Select another encoder.");
    typedef Encoder encoder_type;
    static constexpr pthash_search_type search = Search;
    static constexpr bool minimal = Minimal;

    template <typename Iterator>
    build_timings build_in_internal_memory(Iterator keys, const uint64_t num_keys,
                                           build_configuration const& config) {
        build_configuration build_config = set_build_configuration(config);
        internal_memory_builder_single_phf<Hasher, Bucketer> builder;
        auto timings = builder.build_from_keys(keys, num_keys, build_config);
        timings.encoding_microseconds = build(builder, build_config);
        return timings;
    }

    template <typename Iterator>
    build_timings build_in_external_memory(Iterator keys, const uint64_t num_keys,
                                           build_configuration const& config) {
        build_configuration build_config = set_build_configuration(config);
        external_memory_builder_single_phf<Hasher, Bucketer> builder;
        auto timings = builder.build_from_keys(keys, num_keys, build_config);
        timings.encoding_microseconds = build(builder, build_config);
        return timings;
    }

    template <typename Builder>
    uint64_t build(Builder const& builder, build_configuration const& config) {
        auto start = clock_type::now();

        if (Minimal != config.minimal) {
            throw std::runtime_error(  //
                "template parameter 'Minimal' must be equal to config.minimal");
        }
        if (Search != config.search) {
            throw std::runtime_error(  //
                "template parameter 'Search' must be equal to config.search");
        }

        m_seed = builder.seed();
        m_num_keys = builder.num_keys();
        m_table_size = builder.table_size();
        m_M_128 = fastmod::computeM_u64(m_table_size);
        m_M_64 = fastmod::computeM_u32(m_table_size);
        m_bucketer = builder.bucketer();
        m_pilots.encode(builder.pilots().data(), m_bucketer.num_buckets());
        if (Minimal and m_num_keys < m_table_size) {
            assert(builder.free_slots().size() == m_table_size - m_num_keys);
            m_free_slots.encode(builder.free_slots().begin(), m_table_size - m_num_keys);
        }
        auto stop = clock_type::now();

        return to_microseconds(stop - start);
    }

    template <typename T>
    uint64_t operator()(T const& key) const {
        fprintf(stderr, "[P4] ENTER single_phf::operator()\n");
        fprintf(stderr, "[P4] Hasher::hash(key, m_seed=%llu) ...\n", (unsigned long long)m_seed);
        auto hash = Hasher::hash(key, m_seed);
        fprintf(stderr, "[P4]   ... returned hash={%llu, %llu}\n", (unsigned long long)hash.first(), (unsigned long long)hash.second());
        fprintf(stderr, "[P4] Calling position(hash)...\n");
        uint64_t final_pos = position(hash);
        fprintf(stderr, "[P4] EXIT single_phf::operator() -> %llu\n", (unsigned long long)final_pos);
        return final_pos;
    }

    uint64_t position(typename Hasher::hash_type hash) const {
        fprintf(stderr, "[P4] ENTER single_phf::position(hash={%llu, %llu})\n", (unsigned long long)hash.first(), (unsigned long long)hash.second());

        fprintf(stderr, "[P4] Calling m_bucketer.bucket(hash.first()=%llu)...\n", (unsigned long long)hash.first());
        const uint64_t bucket_id = m_bucketer.bucket(hash.first());
        fprintf(stderr, "[P4] m_bucketer.bucket returned bucket_id: %llu\n", (unsigned long long)bucket_id);

        fprintf(stderr, "[P4] Calling m_pilots.access(bucket=%llu)...\n", (unsigned long long)bucket_id);
        const uint64_t pilot = m_pilots.access(bucket_id);
        fprintf(stderr, "[P4] m_pilots.access returned pilot: %llu\n", (unsigned long long)pilot);

        uint64_t p = 0;
        if constexpr (Search == pthash_search_type::xor_displacement) {
            fprintf(stderr, "[P4] Using XOR displacement...\n");
            fprintf(stderr, "[P4] Calculating hashed_pilot = default_hash64(pilot=%llu, m_seed=%llu)...\n", (unsigned long long)pilot, (unsigned long long)m_seed);
            const uint64_t hashed_pilot = default_hash64(pilot, m_seed);
            fprintf(stderr, "[P4]   hashed_pilot = %llu\n", (unsigned long long)hashed_pilot);
            uint64_t xor_result = hash.second() ^ hashed_pilot;
            fprintf(stderr, "[P4] Calculating p = fastmod::fastmod_u64(val=%llu ^ %llu = %llu, M=m_M_128, N=m_table_size=%llu)...\n",
                    (unsigned long long)hash.second(), (unsigned long long)hashed_pilot, (unsigned long long)xor_result, (unsigned long long)m_table_size);
            p = fastmod::fastmod_u64(xor_result, m_M_128, m_table_size);
            fprintf(stderr, "[P4]   Calculated p = %llu\n", (unsigned long long)p);
        } else {
            // This path corresponds to the description, assuming ADD displacement is the `else`
            fprintf(stderr, "[P4] Using ADD displacement...\n");
            fprintf(stderr, "[P4] Calculating s = fastmod::fastdiv_u32(pilot=%llu, M=m_M_64)...\n", (unsigned long long)pilot);
            const uint64_t s = fastmod::fastdiv_u32(pilot, m_M_64);
            fprintf(stderr, "[P4]   s = %llu\n", (unsigned long long)s);
            uint64_t sum_hash_s = hash.second() + s;
            fprintf(stderr, "[P4] Calculating intermediate_hash = hash64(hash.second() + s = %llu + %llu = %llu).mix()...\n",
                    (unsigned long long)hash.second(), (unsigned long long)s, (unsigned long long)sum_hash_s);
            uint64_t intermediate_hash_mix = hash64(sum_hash_s).mix();
            fprintf(stderr, "[P4]   intermediate_hash_mix = %llu\n", (unsigned long long)intermediate_hash_mix);
            uint64_t shifted_mix = intermediate_hash_mix >> 33;
            uint64_t sum_for_mod = shifted_mix + pilot;
            fprintf(stderr, "[P4] Calculating p = fastmod::fastmod_u32(val=(intermediate_hash_mix >> 33) + pilot = (%llu >> 33) + %llu = %llu + %llu = %llu, M=m_M_64, N=m_table_size=%llu)...\n",
                    (unsigned long long)intermediate_hash_mix, (unsigned long long)pilot, (unsigned long long)shifted_mix, (unsigned long long)pilot, (unsigned long long)sum_for_mod, (unsigned long long)m_table_size);
            p = fastmod::fastmod_u32(sum_for_mod, m_M_64, m_table_size);
            fprintf(stderr, "[P4]   Calculated p = %llu\n", (unsigned long long)p);
        }

        if constexpr (Minimal) {
            fprintf(stderr, "[P4] Minimal=true. Checking if p (%llu) < num_keys() (%llu)...\n", (unsigned long long)p, (unsigned long long)num_keys());
            if (PTHASH_LIKELY(p < num_keys())) {
                fprintf(stderr, "[P4]   p < num_keys(). Returning p.\n");
                fprintf(stderr, "[P4] EXIT single_phf::position -> %llu\n", (unsigned long long)p);
                return p;
            } else {
                 fprintf(stderr, "[P4]   p >= num_keys()...\n");
                 uint64_t index = p - num_keys();
                 fprintf(stderr, "[P4]   ... Calling m_free_slots.access(index=%llu)...\n", (unsigned long long)index);
                 uint64_t final_pos = m_free_slots.access(index);
                 fprintf(stderr, "[P4]   m_free_slots.access returned final_pos: %llu\n", (unsigned long long)final_pos);
                 fprintf(stderr, "[P4] EXIT single_phf::position -> %llu\n", (unsigned long long)final_pos);
                 return final_pos;
            }
        } else {
            fprintf(stderr, "[P4] Minimal=false. Returning p.\n");
            fprintf(stderr, "[P4] EXIT single_phf::position -> %llu\n", (unsigned long long)p);
            return p;
        }
    }

    uint64_t num_bits_for_pilots() const {
        return 8 * (sizeof(m_seed) + sizeof(m_num_keys) + sizeof(m_table_size) + sizeof(m_M_64) +
                    sizeof(m_M_128)) +
               m_pilots.num_bits();
    }

    uint64_t num_bits_for_mapper() const {
        return m_bucketer.num_bits() + m_free_slots.num_bytes() * 8;
    }

    uint64_t num_bits() const {
        return num_bits_for_pilots() + num_bits_for_mapper();
    }

    uint64_t num_keys() const {
        return m_num_keys;
    }

    uint64_t table_size() const {
        return m_table_size;
    }

    uint64_t seed() const {
        return m_seed;
    }

    // ========= START AGGRESSIVE GETTERS =========
    uint64_t get_seed() const {
        return m_seed;
    }

    uint64_t get_num_keys() const {
        return m_num_keys;
    }

    uint64_t get_table_size() const {
        return m_table_size;
    }

    __uint128_t get_M_128() const { // Corrected capitalization
        return m_M_128;
    }

    uint64_t get_M_64() const { // Corrected capitalization
        return m_M_64;
    }

    const Bucketer& get_bucketer() const {
        return m_bucketer;
    }

    const Encoder& get_pilots() const {
        return m_pilots;
    }

    const bits::elias_fano<false, false>& get_free_slots() const {
        return m_free_slots;
    }

    // Added for detailed sample intermediate output
    uint64_t position_raw(typename Hasher::hash_type hash) const {
        const uint64_t bucket = m_bucketer.bucket(hash.first());
        const uint64_t pilot = m_pilots.access(bucket);

        uint64_t p = 0;
        if constexpr (Search == pthash_search_type::xor_displacement) {
            /* xor displacement */
            const uint64_t hashed_pilot = default_hash64(pilot, m_seed);
            p = fastmod::fastmod_u64(hash.second() ^ hashed_pilot, m_M_128, m_table_size);
        } else {
            /* additive displacement */
            const uint64_t s = fastmod::fastdiv_u32(pilot, m_M_64);
            p = fastmod::fastmod_u32(((hash64(hash.second() + s).mix()) >> 33) + pilot, m_M_64,
                                    m_table_size);
        }
        return p;
    }
    // ========= END AGGRESSIVE GETTERS =========

    template <typename Visitor>
    void visit(Visitor& visitor) const {
        visit_impl(visitor, *this);
    }

    template <typename Visitor>
    void visit(Visitor& visitor) {
        visit_impl(visitor, *this);
    }

private:
    template <typename Visitor, typename T>
    static void visit_impl(Visitor& visitor, T&& t) {
        // ======== [P3.PHF] START ========
        fprintf(stderr, "[P3.PHF] ENTER single_phf::visit_impl\n");

        fprintf(stderr, "[P3.PHF] Visiting m_seed... Type: %s, Value: %llu, Addr: %p, Size: %lu\n", typeid(t.m_seed).name(), (unsigned long long)t.m_seed, (void*)&t.m_seed, sizeof(t.m_seed));
        visitor.visit(t.m_seed);

        fprintf(stderr, "[P3.PHF] Visiting m_num_keys... Type: %s, Value: %llu, Addr: %p, Size: %lu\n", typeid(t.m_num_keys).name(), (unsigned long long)t.m_num_keys, (void*)&t.m_num_keys, sizeof(t.m_num_keys));
        visitor.visit(t.m_num_keys);

        fprintf(stderr, "[P3.PHF] Visiting m_table_size... Type: %s, Value: %llu, Addr: %p, Size: %lu\n", typeid(t.m_table_size).name(), (unsigned long long)t.m_table_size, (void*)&t.m_table_size, sizeof(t.m_table_size));
        visitor.visit(t.m_table_size);

        fprintf(stderr, "[P3.PHF] Visiting m_M_128... Type: %s, Value: %016llx%016llx, Addr: %p, Size: %lu\n", typeid(t.m_M_128).name(), (unsigned long long)(t.m_M_128 >> 64), (unsigned long long)t.m_M_128, (void*)&t.m_M_128, sizeof(t.m_M_128));
        visitor.visit(t.m_M_128);

        fprintf(stderr, "[P3.PHF] Visiting m_M_64... Type: %s, Value: %llu, Addr: %p, Size: %lu\n", typeid(t.m_M_64).name(), (unsigned long long)t.m_M_64, (void*)&t.m_M_64, sizeof(t.m_M_64));
        visitor.visit(t.m_M_64);

        fprintf(stderr, "[P3.PHF] Visiting m_bucketer... Type: %s, Addr: %p\n", typeid(t.m_bucketer).name(), (void*)&t.m_bucketer);
        visitor.visit(t.m_bucketer);

        fprintf(stderr, "[P3.PHF] Visiting m_pilots... Type: %s, Addr: %p\n", typeid(t.m_pilots).name(), (void*)&t.m_pilots);
        visitor.visit(t.m_pilots);

        fprintf(stderr, "[P3.PHF] Visiting m_free_slots... Type: %s, Addr: %p\n", typeid(t.m_free_slots).name(), (void*)&t.m_free_slots);
        visitor.visit(t.m_free_slots);

        fprintf(stderr, "[P3.PHF] EXIT single_phf::visit_impl\n");
        // ======== [P3.PHF] END ========
    }

    static build_configuration set_build_configuration(build_configuration const& config) {
        build_configuration build_config = config;
        if (config.minimal != Minimal) {
            if (config.verbose) {
                std::cout << "setting config.verbose = " << (Minimal ? "true" : "false")
                          << std::endl;
            }
            build_config.minimal = Minimal;
        }
        if (config.search != Search) {
            if (config.verbose) { std::cout << "setting config.search = " << Search << std::endl; }
            build_config.search = Search;
        }
        return build_config;
    }

    uint64_t m_seed;
    uint64_t m_num_keys;
    uint64_t m_table_size;
    __uint128_t m_M_128;
    uint64_t m_M_64;
    Bucketer m_bucketer;
    Encoder m_pilots;
    bits::elias_fano<false, false> m_free_slots;
};

}  // namespace pthash