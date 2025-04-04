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
#include "utils/util.hpp" // For constants
#include "utils/hasher.hpp" // For default_hash64 used in logging
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

    // ---- Sample Keys (Keep this for lookup verification) ----
    std::vector<uint64_t> sample_keys = {
        3305430968978464066ULL, 13481878520173671680ULL, 15019645936901674592ULL,
        9982081833606184227ULL, 8636735673839951836ULL, 11008782874310338137ULL,
        7163182426250525475ULL, 18235418287357999760ULL, 12843002247398813397ULL,
        14261303737189920788ULL
    };
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
            config.verbose = false; // Don't enable verbose mode - keep stdout clean for JSON
        } else {
             config.verbose = false; // Don't enable verbose mode - keep stdout clean for JSON
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
                 throw std::runtime_error("PHF index out of bounds!");
            }
            reordered_values[phf_index] = values[i];
            final_indices[i] = phf_index; // Store final index
        }
        std::cerr << "Values reordered." << std::endl;

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
