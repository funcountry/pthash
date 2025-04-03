#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <stdexcept>
#include <cstdint>
#include <chrono>
#include <iomanip> // For std::setw
#include <numeric> // For std::iota
#include <cstdio>  // For fprintf
#include <unordered_map> // For sample key lookup
#include <thread>

// Include necessary PTHash headers
#include "pthash.hpp"       // Main PTHash header
#include "essentials.hpp"   // For saving/loading
#include "nlohmann/json.hpp" // nlohmann/json library
#include "utils/util.hpp" // For constants
#include "utils/hasher.hpp" // For default_hash64 used in logging
#include "utils/instrumentation.hpp"
#include "single_phf.hpp"

// Define the specific PTHash configuration we want to use
// *** MUST MATCH THE CONFIGURATION USED IN THE ORIGINAL BUILD & WRAPPER ***
using hasher = pthash::murmurhash2_64;
using bucketer = pthash::skew_bucketer;
using encoder = pthash::dictionary_dictionary; // This is dual<dictionary, dictionary>
constexpr bool minimal_build = true;
constexpr pthash::pthash_search_type search_type = pthash::pthash_search_type::xor_displacement;

using pthash_builder_type = pthash::internal_memory_builder_single_phf<hasher, pthash::skew_bucketer>;
using pthash_function_type = pthash::single_phf<hasher, pthash::skew_bucketer, pthash::dictionary_dictionary, minimal_build, search_type>;

// Helper function to read binary uint64_t keys from file
std::vector<uint64_t> read_keys(const std::string& filename) {
    std::ifstream input(filename, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open key file: " + filename);
    }
    
    uint64_t count;
    input.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!input) {
        throw std::runtime_error("Failed to read key count from " + filename);
    }
    
    std::vector<uint64_t> keys(count);
    input.read(reinterpret_cast<char*>(keys.data()), count * sizeof(uint64_t));
    if (!input) {
        throw std::runtime_error("Failed to read all keys from " + filename);
    }
    
    return keys;
}

// Helper function to read binary uint16_t values from file
std::vector<uint16_t> read_values(const std::string& filename) {
    std::ifstream input(filename, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open value file: " + filename);
    }
    
    uint64_t count;
    input.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!input) {
        throw std::runtime_error("Failed to read value count from " + filename);
    }
    
    std::vector<uint16_t> values(count);
    input.read(reinterpret_cast<char*>(values.data()), count * sizeof(uint16_t));
    if (!input) {
        throw std::runtime_error("Failed to read all values from " + filename);
    }
    
    return values;
}

// --- NEW Helper Functions for JSON Generation ---

nlohmann::json get_bit_vector_details(const bits::bit_vector& bv) {
    nlohmann::json j;
    // *** DEBUG PRINT ***
    PTHASH_LOG("[DEBUG] get_bit_vector_details: bv.num_bits()=%llu, bv.data().size()=%lu\n",
            (unsigned long long)bv.num_bits(), (unsigned long)bv.data().size());
    // --- END DEBUG PRINT ---
    j["NumBits"] = bv.num_bits();
    j["DataVecLen"] = bv.data().size(); // Use existing data() method
    return j;
}

// Add a helper function for std::vector<uint64_t>
nlohmann::json get_vector_uint64_details(const std::vector<uint64_t>& vec) {
    nlohmann::json j;
    // *** DEBUG PRINT ***
    PTHASH_LOG("[DEBUG] get_vector_uint64_details: vec.size()=%lu\n", (unsigned long)vec.size());
    // --- END DEBUG PRINT ---
    j["Size"] = vec.size();
    return j;
}

nlohmann::json get_compact_vector_details(const bits::compact_vector& cv) {
    nlohmann::json j;
    // *** DEBUG PRINT ***
    PTHASH_LOG("[DEBUG] get_compact_vector_details: cv.size()=%llu, cv.width()=%llu, cv.data().size()=%lu\n",
           (unsigned long long)cv.size(), (unsigned long long)cv.width(), (unsigned long)cv.data().size());
    // --- END DEBUG PRINT ---
    j["Size"] = cv.size(); // Use existing size() method
    j["Width"] = cv.width(); // Use existing width() method
    // Calculate mask based on width
    uint64_t width = cv.width();
    j["Mask"] = (width == 64) ? uint64_t(-1) : ((uint64_t(1) << width) - 1);
    j["Data"] = get_vector_uint64_details(cv.data()); // Use specific vector helper
    return j;
}

nlohmann::json get_dictionary_details(const pthash::dictionary& d) {
     nlohmann::json j;
     // Use public getters we defined
     j["Ranks"] = get_compact_vector_details(d.get_ranks());
     j["Dict"] = get_compact_vector_details(d.get_dict());
     return j;
}

nlohmann::json get_dictionary_dictionary_details(const pthash::dictionary_dictionary& dd) {
     nlohmann::json j;
     // dd is dual<dictionary, dictionary>
     j["Front"] = get_dictionary_details(dd.get_front()); // Use temporary getter
     j["Back"] = get_dictionary_details(dd.get_back());   // Use temporary getter
     return j;
}

nlohmann::json get_elias_fano_details(const bits::elias_fano<false, false>& ef) {
     nlohmann::json j;
     // In the C++ structure, ef.get_back() contains the universe size (m_back)
     // And ef.size() returns the number of keys (m_low_bits.size())
     j["UniverseSize"] = ef.get_back(); // Include universe size for debugging
     j["NumKeys"] = ef.size(); // Use existing size() method

     // Debug print for troubleshooting serialization vs loading
     PTHASH_LOG("[DEBUG] EliasFano details: UniverseSize=%llu, NumKeys=%llu\n",
             (unsigned long long)ef.get_back(), (unsigned long long)ef.size());
     // --- END DEBUG PRINT ---

     // Use public getters we defined
     j["HighBits"] = get_bit_vector_details(ef.get_high_bits());
     j["LowBits"] = get_compact_vector_details(ef.get_low_bits());
     return j;
}


nlohmann::json get_skew_bucketer_details(const pthash::skew_bucketer& b) {
    nlohmann::json j;
    j["NumDense"] = b.get_num_dense_buckets(); // Use temporary getter
    j["NumSparse"] = b.get_num_sparse_buckets(); // Use temporary getter
    __uint128_t m_dense = b.get_M_dense();       // Use temporary getter
    __uint128_t m_sparse = b.get_M_sparse();     // Use temporary getter
    j["MDenseH"] = uint64_t(m_dense >> 64);
    j["MDenseL"] = uint64_t(m_dense);
    j["MSparseH"] = uint64_t(m_sparse >> 64);
    j["MSparseL"] = uint64_t(m_sparse);
    return j;
}


// --- Main Function ---
int main(int argc, char** argv) {
    if (argc < 6 || argc > 7) { // Adjusted for optional seed
        std::cerr << "Usage: " << argv[0]
                  << " <keys.bin> <values.bin> <output.phf> <alpha> <lambda> [seed]" << std::endl;
        return 1;
    }

    std::string keys_filename = argv[1];
    std::string values_filename = argv[2];
    std::string output_filename = argv[3];
    double alpha = std::stod(argv[4]);
    double lambda = std::stod(argv[5]);
    uint64_t fixed_seed = (argc == 7) ? std::stoull(argv[6]) : pthash::constants::invalid_seed; // Use provided seed or mark as invalid if random needed

    bool generate_details = (argc == 7); // Generate details only if seed is provided

    // ---- Sample Keys (Keep this for lookup verification) ----
    std::vector<uint64_t> sample_keys = {
        3305430968978464066ULL, 13481878520173671680ULL, 15019645936901674592ULL,
        9982081833606184227ULL, 8636735673839951836ULL, 11008782874310338137ULL,
        7163182426250525475ULL, 18235418287357999760ULL, 12843002247398813397ULL,
        14261303737189920788ULL
    };
    size_t num_samples = sample_keys.size();
    // --------------------------------------------------------------

    try {
        std::cerr << "Reading keys and values..." << std::endl;
        auto keys = read_keys(keys_filename);
        auto values = read_values(values_filename);

        if (keys.size() != values.size()) throw std::runtime_error("Key/value counts mismatch!");
        if (keys.empty()) throw std::runtime_error("Input keys empty!");

        uint64_t num_keys = keys.size();
        pthash_builder_type builder;
        pthash::build_configuration config;
        config.alpha = alpha;
        // Regen seed if needed
        if (fixed_seed == pthash::constants::invalid_seed) {
            fixed_seed = pthash::random_value();
            config.verbose = true; // Only make verbose if seed is random
        } else {
             config.verbose = true; // Always be verbose to see timing details
        }
        config.lambda = lambda;
        config.seed = fixed_seed; // Use the fixed seed
        config.search = search_type;
        config.minimal = minimal_build;
        config.num_threads = std::thread::hardware_concurrency();
        // config.verbose is set above based on generate_details

        std::cerr << "Building PHF (Seed: " << config.seed << ", Alpha: " << config.alpha 
                  << ", Lambda: " << config.lambda 
                  << ", Threads: " << config.num_threads << ")..." << std::endl;
        auto timings = builder.build_from_keys(keys.begin(), num_keys, config);

        pthash_function_type mphf;
        uint64_t encode_time = mphf.build(builder, config);
        timings.encoding_microseconds = encode_time;
        std::cerr << "PHF built." << std::endl;

        // Reorder values (same as before)
        std::cerr << "Reordering values..." << std::endl;
        std::vector<uint16_t> reordered_values(num_keys);
        // Precompute indices to avoid repeated lookups during sample extraction if needed later
        std::vector<uint64_t> final_indices(num_keys);
        for (uint64_t i = 0; i < num_keys; ++i) {
            uint64_t phf_index = mphf(keys[i]);
            if (phf_index >= num_keys) {
                 PTHASH_LOG("CRITICAL ERROR: PHF index %llu out of bounds for key %llu (num_keys=%llu)!\n",
                         (unsigned long long)phf_index, (unsigned long long)keys[i], (unsigned long long)num_keys);
                 throw std::runtime_error("PHF index out of bounds!");
            }
            reordered_values[phf_index] = values[i];
            final_indices[i] = phf_index; // Store final index
        }
        std::cerr << "Values reordered." << std::endl;

        // --- Generate COMPLETE Ground Truth JSON ---
        if (generate_details) {
            std::cerr << "Generating COMPLETE ground truth details..." << std::endl;
            nlohmann::json gt;

            // Basic Info
            gt["seed"] = mphf.get_seed();
            gt["num_keys"] = mphf.get_num_keys();
            gt["build_params"]["alpha"] = alpha;
            gt["build_params"]["lambda"] = lambda;

            // Header Params (using getters)
            gt["header_params"]["table_size"] = mphf.get_table_size();
            __uint128_t M128 = mphf.get_M_128(); // Correct capitalization in getter name
            uint64_t M64 = mphf.get_M_64();      // Correct capitalization in getter name
            gt["header_params"]["M128High"] = uint64_t(M128 >> 64);
            gt["header_params"]["M128Low"] = uint64_t(M128);
            gt["header_params"]["M64"] = M64;

            // Bucketer Params (using getter + helper)
            gt["bucketer_params"] = get_skew_bucketer_details(mphf.get_bucketer());

            // Pilot Structure (using getter + helper)
            gt["pilot_structure"] = get_dictionary_dictionary_details(mphf.get_pilots());

            // Free Slots Structure (using getter + helper)
            gt["free_slots_structure"] = get_elias_fano_details(mphf.get_free_slots());

            // --- Sample Key Details ---
            nlohmann::json sample_hashes = nlohmann::json::object();
            nlohmann::json sample_buckets = nlohmann::json::object();
            nlohmann::json sample_pilots = nlohmann::json::object(); // Stores pilot per *bucket*
            nlohmann::json sample_positions = nlohmann::json::object();
            nlohmann::json sample_final_indices = nlohmann::json::object(); // Store final index
            nlohmann::json sample_reordered_values = nlohmann::json::object(); // Store value at final index
            nlohmann::json sample_original_values = nlohmann::json::array(); // Also capture original values for samples
            nlohmann::json sample_lookup_phase5 = nlohmann::json::object(); // NEW for phase 5 intermediates

            // Find original indices of sample keys
            std::vector<uint64_t> sample_indices;
            std::unordered_map<uint64_t, uint64_t> key_to_original_index;
            for(uint64_t i=0; i<keys.size(); ++i) key_to_original_index[keys[i]] = i;

            for (const auto& sk : sample_keys) {
                 auto it = key_to_original_index.find(sk);
                 if (it != key_to_original_index.end()) {
                     sample_indices.push_back(it->second);
                 } else {
                     PTHASH_LOG("Warning: Sample key %llu not found in input keys!\n", (unsigned long long)sk);
                 }
            }


            std::vector<uint64_t> sample_key_list_for_json;
             // Limit to actual found samples or requested number, whichever is smaller
             size_t actual_samples = std::min(sample_indices.size(), num_samples);
             for (size_t i = 0; i < actual_samples; ++i) {
                uint64_t original_idx = sample_indices[i];
                uint64_t key = keys[original_idx];
                uint16_t original_value = values[original_idx]; // Get original value
                std::string key_str = std::to_string(key);

                sample_key_list_for_json.push_back(key); // Add key to list
                sample_original_values.push_back(original_value); // Add original value to list


                // --- Phase 5 Intermediates ---
                auto h = hasher::hash(key, mphf.get_seed());
                uint64_t h1 = h.first(); // Used for bucketing
                uint64_t h2 = h.second(); // Used for displacement

                // DEBUG INSTRUMENTATION: Print internal M values from bucketer
                PTHASH_LOG_VARS(__uint128_t m_dense = mphf.get_bucketer().get_M_dense());
                PTHASH_LOG_VARS(__uint128_t m_sparse = mphf.get_bucketer().get_M_sparse());
                
                PTHASH_LOG("[BUILD_PHF DEBUG] Key=%llu, h1=0x%llx\n", (unsigned long long)key, (unsigned long long)h1);
                PTHASH_LOG("[BUILD_PHF DEBUG] m_M_dense H=0x%llx L=0x%llx\n", 
                        (unsigned long long)(m_dense >> 64), (unsigned long long)m_dense);
                PTHASH_LOG("[BUILD_PHF DEBUG] m_M_sparse H=0x%llx L=0x%llx\n", 
                        (unsigned long long)(m_sparse >> 64), (unsigned long long)m_sparse);
                
                // Store both hashes for clarity, even if they are the same for murmurhash2_64
                sample_hashes[key_str] = {h1, h2};
                uint64_t bucket_id = mphf.get_bucketer().bucket(h.first());
                sample_lookup_phase5[key_str] = { {"hash1", h1}, {"bucket_id", bucket_id} };
                // --- End Phase 5 Intermediates ---
                sample_buckets[key_str] = bucket_id;

                uint64_t pilot_val = mphf.get_pilots().access(bucket_id);
                sample_pilots[std::to_string(bucket_id)] = pilot_val; // Store pilot per bucket_id

                uint64_t pos_raw = mphf.position_raw(h); // Use temporary raw position getter
                sample_positions[key_str] = pos_raw;

                uint64_t final_index = final_indices[original_idx]; // Use precomputed final index
                sample_final_indices[key_str] = final_index;
                sample_reordered_values[key_str] = reordered_values[final_index];
            }

            // Add sample data lists to JSON
            gt["sample_data"]["keys"] = sample_key_list_for_json;
            gt["sample_data"]["original_values"] = sample_original_values;


            // Add intermediate and final lookup data maps to JSON
            gt["sample_key_hashes"] = sample_hashes;
            gt["sample_key_buckets"] = sample_buckets;
            gt["sample_lookup_phase5"] = sample_lookup_phase5; // Add phase 5 intermediates
            gt["sample_bucket_pilots"] = sample_pilots;
            gt["sample_key_raw_positions"] = sample_positions; // Renamed for clarity
            gt["sample_final_indices"] = sample_final_indices; // Added final index
            gt["sample_reordered_values"] = sample_reordered_values; // Renamed for clarity

            // --- ADD DARRAY DETAILS to free_slots_structure JSON ---
            nlohmann::json darray1_details;
            const auto& d1 = mphf.get_free_slots().get_high_bits_d1(); // Use getter
            darray1_details["Positions"] = d1.getNumPositions();         // Use existing getter
            darray1_details["BlockInventory"] = d1.getBlockInventory(); // Use existing getter
            darray1_details["SubBlockInventory"] = d1.getSubblockInventory(); // Use existing getter
            darray1_details["OverflowPositions"] = d1.getOverflowPositions(); // Use existing getter
            gt["free_slots_structure"]["DArray1_Details"] = darray1_details; // Add to JSON

            nlohmann::json darray0_details;
            const auto& d0 = mphf.get_free_slots().get_high_bits_d0(); // Use getter
            darray0_details["Positions"] = d0.getNumPositions();
            darray0_details["BlockInventory"] = d0.getBlockInventory();
            darray0_details["SubBlockInventory"] = d0.getSubblockInventory();
            darray0_details["OverflowPositions"] = d0.getOverflowPositions();
            gt["free_slots_structure"]["DArray0_Details"] = darray0_details; // Add to JSON

            std::cerr << "Added DArray inventory details to ground truth JSON." << std::endl;
            // --- END ADDING DARRAY DETAILS ---

            // Output the detailed JSON to stdout for the script to capture
            std::cout << gt.dump(2) << std::endl; // Use indent 2 for readability
            std::cerr << "Ground truth details generated and printed to stdout." << std::endl;

        }


        // Save PHF and reordered values (same as before)
        std::cerr << "Saving PHF and values to " << output_filename << "..." << std::endl;
        essentials::save(mphf, output_filename.c_str()); // Save the PHF structure itself
        std::ofstream os(output_filename, std::ios::binary | std::ios::app); // Append values
        if (!os) throw std::runtime_error("Cannot open output file for appending: " + output_filename);
        uint64_t value_count = reordered_values.size();
        // Write count THEN values
        os.write(reinterpret_cast<const char*>(&value_count), sizeof(value_count));
        os.write(reinterpret_cast<const char*>(reordered_values.data()), value_count * sizeof(uint16_t));
        if (!os) throw std::runtime_error("Error writing values to output file");
        os.close();
        std::cerr << "Saved data." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
