#include <iostream>
#include <vector>
#include <cstdint>
#include <iomanip> // For std::hex, std::setw, std::setfill
#include <cstdio>  // For fprintf

// Include the header containing fastmod
#include "fastmod.h" // Use the include path configured in CMakeLists.txt

// Structure to hold test case data
struct FastModTestCase {
    std::string description;
    uint64_t a;
    uint64_t m_high; // M high part (as read from Go struct)
    uint64_t m_low;  // M low part (as read from Go struct)
    uint64_t d;      // Divisor 'n'
    uint64_t expected_result;
};

int main() {
    std::cerr << "--- Starting fastmod::fastmod_u64 Test ---" << std::endl;

    // --- Test Cases ---
    // Example 1: Key 0 (Dense Path)
    // [LP5]   Calling fastmod_u64(hash=10978613219408062656, M_dense=0x01DE5D6E3F8868A4701DE5D6E3F8868B, num_dense=137)
    // Go loads M_dense as: High=8078866017683015307 (0x701DE5D6E3F8868B), Low=134647766961383588 (0x01DE5D6E3F8868A4)
    // [LP5]   fastmod_u64 result (dense) = 90
    FastModTestCase case1 = {
        "Key 0 (Dense)",
        10978613219408062656ULL, // a (hash1)
        134647766961383588ULL,   // M_high (Go perspective)
        8078866017683015307ULL,  // M_low (Go perspective)
        137ULL,                  // d (num_dense_buckets)
        90ULL                    // Expected result
    };

    // Example 2: Key 1 (Sparse Path)
    // [LP5]   Calling fastmod_u64(hash=18424673762719242200, M_sparse=0x00CB8727C065C393E032E1C9F01970E5, num_sparse=322)
    // Go loads M_sparse as: High=16155223070764265701 (0xE032E1C9F01970E5), Low=57288025073632147 (0x00CB8727C065C393)
    // [LP5]   fastmod_u64 result (sparse_mod) = 28
    FastModTestCase case2 = {
        "Key 1 (Sparse Mod)",
        18424673762719242200ULL, // a (hash1)
        57288025073632147ULL,    // M_high (Go perspective)
        16155223070764265701ULL, // M_low (Go perspective)
        322ULL,                  // d (num_sparse_buckets)
        28ULL                    // Expected result (just the modulo part)
    };
    
    // Example 3: Key 9 (Sparse Path)
    // [LP5]   Calling fastmod_u64(hash=12589684530584323697, M_sparse=0x00CB8727C065C393E032E1C9F01970E5, num_sparse=322)
    // Go loads M_sparse as: High=16155223070764265701 (0xE032E1C9F01970E5), Low=57288025073632147 (0x00CB8727C065C393)
    // [LP5]   fastmod_u64 result (sparse_mod) = 31
    FastModTestCase case3 = {
        "Key 9 (Sparse Mod)",
        12589684530584323697ULL, // a (hash1)
        57288025073632147ULL,    // M_high (Go perspective)
        16155223070764265701ULL, // M_low (Go perspective)
        322ULL,                  // d (num_sparse_buckets)
        31ULL                    // Expected result (just the modulo part)
    };

    std::vector<FastModTestCase> test_cases = {case1, case2, case3};

    // --- Run Tests ---
    bool all_passed = true;
    for (const auto& tc : test_cases) {
        std::cerr << "\n--- Running Test Case: " << tc.description << " ---" << std::endl;
        std::cerr << "Inputs: a=" << tc.a << ", M_H=0x" << std::hex << tc.m_high << ", M_L=0x" << tc.m_low << ", d=" << std::dec << tc.d << std::endl;

        // First try original way (as in the previous test)
        __uint128_t M_original = static_cast<__uint128_t>(tc.m_low) << 64 | tc.m_high;
        std::cerr << "Original M ordering (M_low << 64 | M_high) = 0x" 
                  << std::hex << std::setw(16) << std::setfill('0') << (uint64_t)(M_original >> 64) 
                  << std::setw(16) << std::setfill('0') << (uint64_t)M_original << std::dec << std::endl;

        // Now try swapping high and low
        __uint128_t M_swapped = static_cast<__uint128_t>(tc.m_high) << 64 | tc.m_low;
        std::cerr << "Swapped M ordering  (M_high << 64 | M_low) = 0x" 
                  << std::hex << std::setw(16) << std::setfill('0') << (uint64_t)(M_swapped >> 64) 
                  << std::setw(16) << std::setfill('0') << (uint64_t)M_swapped << std::dec << std::endl;

        // Try both orderings
        uint64_t result_original = fastmod::fastmod_u64(tc.a, M_original, tc.d);
        uint64_t result_swapped = fastmod::fastmod_u64(tc.a, M_swapped, tc.d);
        
        std::cerr << "Result with original ordering: " << result_original << std::endl;
        std::cerr << "Result with swapped ordering: " << result_swapped << std::endl;
        
        // Check which one matches the expected result
        bool original_matches = (result_original == tc.expected_result);
        bool swapped_matches = (result_swapped == tc.expected_result);
        
        if (original_matches) {
            std::cerr << "Original ordering PASSED!" << std::endl;
        } else {
            std::cerr << "Original ordering FAILED (Expected: " << tc.expected_result << ")" << std::endl;
        }
        
        if (swapped_matches) {
            std::cerr << "Swapped ordering PASSED!" << std::endl;
        } else {
            std::cerr << "Swapped ordering FAILED (Expected: " << tc.expected_result << ")" << std::endl;
        }
        
        all_passed = all_passed && (original_matches || swapped_matches);
    }

    std::cerr << "\n--- Test Summary ---" << std::endl;
    if (all_passed) {
        std::cerr << "Test PASSED with at least one ordering matching expected results." << std::endl;
        return 0;
    } else {
        std::cerr << "Test FAILED: Neither ordering matched expected results in all cases." << std::endl;
        return 1;
    }
} 