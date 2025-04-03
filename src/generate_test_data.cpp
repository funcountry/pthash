#include <iostream>
#include <fstream>
#include <random>
#include <vector>
#include <stdint.h>

// Generate random keys and values for testing
int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <num_keys> <output_prefix>" << std::endl;
        return 1;
    }

    uint64_t num_keys = std::stoull(argv[1]);
    std::string output_prefix = argv[2];
    
    std::string keys_filename = output_prefix + ".keys.bin";
    std::string values_filename = output_prefix + ".values.bin";

    // Generate random keys
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> key_dist;
    std::uniform_int_distribution<uint16_t> value_dist;

    std::vector<uint64_t> keys(num_keys);
    std::vector<uint16_t> values(num_keys);

    std::cout << "Generating " << num_keys << " random keys and values..." << std::endl;
    for (uint64_t i = 0; i < num_keys; ++i) {
        keys[i] = key_dist(gen);
        values[i] = value_dist(gen);
    }

    // Save the keys to file
    std::ofstream keys_file(keys_filename, std::ios::binary);
    if (!keys_file) {
        std::cerr << "Cannot open output file: " << keys_filename << std::endl;
        return 1;
    }
    keys_file.write(reinterpret_cast<const char*>(&num_keys), sizeof(num_keys));
    keys_file.write(reinterpret_cast<const char*>(keys.data()), num_keys * sizeof(uint64_t));
    keys_file.close();

    // Save the values to file
    std::ofstream values_file(values_filename, std::ios::binary);
    if (!values_file) {
        std::cerr << "Cannot open output file: " << values_filename << std::endl;
        return 1;
    }
    values_file.write(reinterpret_cast<const char*>(&num_keys), sizeof(num_keys));
    values_file.write(reinterpret_cast<const char*>(values.data()), num_keys * sizeof(uint16_t));
    values_file.close();

    std::cout << "Generated data saved to:" << std::endl;
    std::cout << "  - " << keys_filename << std::endl;
    std::cout << "  - " << values_filename << std::endl;
    
    return 0;
} 