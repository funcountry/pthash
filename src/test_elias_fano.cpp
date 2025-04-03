#include <iostream>
#include <vector>
#include <cstdint>
#include <numeric>   // For std::iota
#include <algorithm> // For std::sort, std::lower_bound
#include <cstdio>    // For fprintf used in instrumentation

// Include necessary PTHash / BITS headers
#include "elias_fano.hpp" // Includes bit_vector.hpp, compact_vector.hpp, darray.hpp, etc.
#include "essentials.hpp" // For util functions if needed, and ensures include paths are okay

// Use the specific Elias-Fano template matching m_free_slots
// Note: template args: index_zeros=false, encode_prefix_sum=false
using ef_type = bits::elias_fano<false, false>;

// Test function for reusable test logic
void run_test_case(const char* test_name, const std::vector<uint64_t>& data, const std::vector<uint64_t>& test_indices) {
    std::cerr << "\n====== " << test_name << " ======" << std::endl;
    
    const uint64_t universe = data.back(); // Universe size based on max value
    const uint64_t num_elements = data.size();

    std::cerr << "Input Data (Sorted): ";
    for(size_t i = 0; i < data.size(); ++i) {
        std::cerr << data[i] << (i == data.size() - 1 ? "" : ", ");
        if (i > 0 && i % 10 == 0) {
            std::cerr << "\n                   ";
        }
    }
    std::cerr << std::endl;
    std::cerr << "Universe Size: " << universe << std::endl;
    std::cerr << "Number of Elements: " << num_elements << std::endl;

    // Instantiate and Encode Elias-Fano
    std::cerr << "\n--- Encoding Elias-Fano ---" << std::endl;
    ef_type ef;
    ef.encode(data.begin(), num_elements, universe);
    std::cerr << "--- Encoding Complete ---" << std::endl;

    // === UPDATED EXPLICIT LOGGING HERE ===
    std::cerr << "\n--- Post-Encoding State ---" << std::endl;
    std::cerr << "ef.size() (m_low_bits.size()): " << ef.size() << std::endl;
    std::cerr << "ef.back() (m_back): " << ef.back() << std::endl;

    // --- HighBits ---
    std::cerr << "ef.m_high_bits.num_bits(): " << ef.get_high_bits().num_bits() << std::endl;
    std::cerr << "ef.m_high_bits.data().size(): " << ef.get_high_bits().data().size() << std::endl;
    if (!ef.get_high_bits().data().empty()) {
        std::cerr << "ef.m_high_bits.data(): [";
        for (size_t i = 0; i < ef.get_high_bits().data().size(); ++i) {
            std::cerr << "0x" << std::hex << ef.get_high_bits().data()[i] << std::dec << (i == ef.get_high_bits().data().size() - 1 ? "" : ", ");
        }
        std::cerr << "]" << std::endl;
    } else {
        std::cerr << "ef.m_high_bits.data(): []" << std::endl;
    }

    // --- LowBits ---
    std::cerr << "ef.m_low_bits.size(): " << ef.get_low_bits().size() << std::endl;
    std::cerr << "ef.m_low_bits.width(): " << ef.get_low_bits().width() << std::endl;
    std::cerr << "ef.m_low_bits.mask(): 0x" << std::hex << ((ef.get_low_bits().width() == 64) ? uint64_t(-1) : ((uint64_t(1) << ef.get_low_bits().width()) - 1)) << std::dec << std::endl;
    std::cerr << "ef.m_low_bits.data().size(): " << ef.get_low_bits().data().size() << std::endl;
    if (!ef.get_low_bits().data().empty()) {
        std::cerr << "ef.m_low_bits.data(): [";
        for (size_t i = 0; i < ef.get_low_bits().data().size(); ++i) {
            std::cerr << "0x" << std::hex << ef.get_low_bits().data()[i] << std::dec << (i == ef.get_low_bits().data().size() - 1 ? "" : ", ");
        }
        std::cerr << "]" << std::endl;
    } else {
        std::cerr << "ef.m_low_bits.data(): []" << std::endl;
    }

    // Formula used for m_high_bits.num_bits() calculation
    uint64_t l = ef.get_low_bits().width();
    uint64_t n = ef.size();
    std::cerr << std::dec << "Calculated m_high_bits.num_bits() using formula: n + (universe >> l) + 1 = " 
              << n << " + (" << universe << " >> " << l << ") + 1 = "
              << n << " + " << (universe >> l) << " + 1 = " 
              << (n + (universe >> l) + 1) << std::endl;
    // ====================================

    // Test access
    std::cerr << "\n--- Testing ef.access(i) ---" << std::endl;

    bool all_passed = true;
    for (uint64_t test_index : test_indices) {
        if (test_index >= num_elements) {
            std::cerr << "Skipping test_index " << test_index << " (out of bounds)" << std::endl;
            continue;
        }

        std::cerr << "\n>>> Testing index i = " << test_index << " <<<" << std::endl;
        uint64_t expected_value = data[test_index];
        std::cerr << "    Expected Original Value: " << expected_value << std::endl;

        // Call ef.access - This will trigger instrumentation logs to stderr
        uint64_t actual_value = ef.access(test_index);

        std::cerr << "    Actual Returned Value:   " << actual_value << std::endl;

        // Verify result on stdout for clarity
        std::cout << test_name << " - Test Index: " << test_index
                  << ", Expected: " << expected_value
                  << ", Got: " << actual_value;
        if (actual_value == expected_value) {
            std::cout << " -> PASS" << std::endl;
        } else {
            std::cout << " -> FAIL" << std::endl;
            all_passed = false;
        }
    }

    std::cerr << "\n--- Test Summary for " << test_name << " ---" << std::endl;
    if (all_passed) {
        std::cerr << "All access tests passed!" << std::endl;
    } else {
        std::cerr << "Some access tests failed!" << std::endl;
    }
}

int main() {
    std::cerr << "--- Elias-Fano Standalone Test (Comprehensive) ---" << std::endl;
    bool all_tests_passed = true;

    // ======== TEST CASE 1: BASIC TEST ========
    // Basic test with small data and common case
    std::vector<uint64_t> basic_data = {3, 8, 10, 15, 21, 22, 30, 31, 45, 50};
    std::vector<uint64_t> basic_test_indices = {0, 3, 5, 9};
    run_test_case("BASIC TEST", basic_data, basic_test_indices);
    
    // ======== TEST CASE 2: MULTI-WORD COMPACT VECTOR TEST ========
    // Force shift + m_width > 64 in compact_vector::access
    std::vector<uint64_t> multi_word_data;
    // Create data with large universe to force l (low bits width) to be large
    // Goal: l ≈ 12 bits
    uint64_t universe_size = 50000;
    for (uint64_t i = 0; i < 10; i++) {
        multi_word_data.push_back(i * (universe_size / 10) + (i+1)*100);  // Create values spread out in range
    }
    // Ensure the data is sorted
    std::sort(multi_word_data.begin(), multi_word_data.end());
    
    // Test i=5 (pos=i*l=5*12=60, shift=60, shift+width=72) and i=6 (pos=6*12=72, block=1, shift=8)
    std::vector<uint64_t> multi_word_indices = {5, 6};
    run_test_case("MULTI-WORD COMPACT VECTOR TEST", multi_word_data, multi_word_indices);
    
    // ======== TEST CASE 3: DARRAY INVENTORY TEST ========
    // Force select to use the inventory system by creating enough values
    // to push data beyond first word
    std::vector<uint64_t> large_data;
    // Create 200+ elements with a reasonably large universe
    uint64_t large_universe = 10000;
    for (uint64_t i = 0; i < 250; i++) {
        large_data.push_back(i * (large_universe / 250) + i*2);  // Spread values
    }
    // Ensure the data is sorted
    std::sort(large_data.begin(), large_data.end());
    
    // Test indices well beyond the first word
    std::vector<uint64_t> inventory_test_indices = {100, 150, 200, 249};
    run_test_case("DARRAY INVENTORY TEST", large_data, inventory_test_indices);
    
    // ======== TEST CASE 4: ZERO LOW BITS TEST (l=0) ========
    // Create a scenario where universe/n is very small to force l=0
    std::vector<uint64_t> zero_l_data;
    // Generate 100 values in a small universe of ~100
    for (uint64_t i = 0; i < 100; i++) {
        zero_l_data.push_back(i+1);  // Values 1-100
    }
    
    // Test various indices
    std::vector<uint64_t> zero_l_indices = {0, 25, 50, 75, 99};
    run_test_case("ZERO LOW BITS TEST (l=0)", zero_l_data, zero_l_indices);
    
    // Final summary
    std::cerr << "\n====== FINAL TEST SUMMARY ======" << std::endl;
    if (all_tests_passed) {
        std::cerr << "All test cases completed successfully!" << std::endl;
        return 0; // Success
    } else {
        std::cerr << "Some test cases failed!" << std::endl;
        return 1; // Failure
    }
} 