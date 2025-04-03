#include <iostream>
#include <vector>
#include <cstdint>
#include <iomanip> // For std::hex, std::setw, std::setfill
#include <cstdio>  // For fprintf

// Include the header containing fastmod
#include "fastmod.h" // Use the include path configured in CMakeLists.txt

// Helper to print __uint128_t
void print_uint128(const char* label, __uint128_t val) {
    uint64_t high = (uint64_t)(val >> 64);
    uint64_t low = (uint64_t)val;
    std::cerr << label << "=0x" << std::hex << std::setw(16) << std::setfill('0') << high
              << std::setw(16) << std::setfill('0') << low << std::dec;
}

// Debug version of fastmod_u64
namespace fastmod_debug {
uint64_t fastmod_u64_debug(uint64_t a, __uint128_t M, uint64_t d) {
     std::cerr << "[C++ fastmod_u64_debug] Input: a=" << a << " (0x" << std::hex << a << std::dec << "), ";
     print_uint128("M", M);
     std::cerr << ", d=" << d << std::endl;

     __uint128_t lowbits = M * a;
     std::cerr << "[C++ fastmod_u64_debug] Step 1 ";
     print_uint128("lowbits", lowbits);
     std::cerr << std::endl;

     // Correct implementation mirroring fastmod::mul128_u64
     __uint128_t bottom_half_prod = (lowbits & UINT64_C(0xFFFFFFFFFFFFFFFF)) * d; // Calculate product of lower 64 bits of lowbits and d
     __uint128_t bottom_half_shifted = bottom_half_prod >> 64; // Get the upper 64 bits of this partial product
     __uint128_t top_half_prod = (lowbits >> 64) * d;          // Calculate product of upper 64 bits of lowbits and d
     __uint128_t both_halves = bottom_half_shifted + top_half_prod; // Add the relevant parts (already shifted down by 64)
     uint64_t result = (uint64_t)(both_halves >> 64);             // Get the final top 64 bits of the combined result

     std::cerr << "[C++ fastmod_u64_debug] Step 2 (Low * d)   "; print_uint128("bottom_half_prod", bottom_half_prod); std::cerr << std::endl;
     std::cerr << "[C++ fastmod_u64_debug] Step 2a(Low>>64)   "; print_uint128("bottom_half_shifted", bottom_half_shifted); std::cerr << std::endl;
     std::cerr << "[C++ fastmod_u64_debug] Step 2b(High * d)  "; print_uint128("top_half_prod", top_half_prod); std::cerr << std::endl;
     std::cerr << "[C++ fastmod_u64_debug] Step 3 (SumHalves) "; print_uint128("both_halves", both_halves); std::cerr << std::endl;
     std::cerr << "[C++ fastmod_u64_debug] Step 4 finalHigh(Sum>>64)= " << result << " (0x" << std::hex << result << std::dec << ")" << std::endl;

     std::cerr << "[C++ fastmod_u64_debug] Return: " << result << std::endl;
     return result;
 }
} // namespace fastmod_debug


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

    // --- Test Cases (same as before) ---
    FastModTestCase case1 = {
        "Key 0 (Dense)",
        10978613219408062656ULL, // a (hash1)
        134647766961383588ULL,   // M_high (Go perspective -> Math High)
        8078866017683015307ULL,  // M_low (Go perspective -> Math Low)
        137ULL,                  // d (num_dense_buckets)
        90ULL                    // Expected result
    };
    FastModTestCase case2 = {
        "Key 1 (Sparse Mod)",
        18424673762719242200ULL, // a (hash1)
        57288025073632147ULL,    // M_high (Go perspective -> Math High)
        16155223070764265701ULL, // M_low (Go perspective -> Math Low)
        322ULL,                  // d (num_sparse_buckets)
        28ULL                    // Expected result (just the modulo part)
    };
    FastModTestCase case3 = {
        "Key 9 (Sparse Mod)",
        12589684530584323697ULL, // a (hash1)
        57288025073632147ULL,    // M_high (Go perspective -> Math High)
        16155223070764265701ULL, // M_low (Go perspective -> Math Low)
        322ULL,                  // d (num_sparse_buckets)
        31ULL                    // Expected result (just the modulo part)
    };
    std::vector<FastModTestCase> test_cases = {case1, case2, case3};

    // --- Run Tests ---
    bool overall_pass = true;
    for (const auto& tc : test_cases) {
        std::cerr << "\n--- Running Test Case: " << tc.description << " ---" << std::endl;
        std::cerr << "Inputs: a=" << tc.a << ", M_H(Go)=0x" << std::hex << tc.m_high << ", M_L(Go)=0x" << tc.m_low << ", d=" << std::dec << tc.d << std::endl;

        // Construct M using the order confirmed by previous test_fastmod: MathHigh << 64 | MathLow
        __uint128_t M_correct = static_cast<__uint128_t>(tc.m_high) << 64 | tc.m_low;
        std::cerr << "Constructed C++ ";
        print_uint128("M_correct", M_correct);
        std::cerr << std::endl;


        std::cerr << "--- Calling C++ Debug ---" << std::endl;
        uint64_t result_debug = fastmod_debug::fastmod_u64_debug(tc.a, M_correct, tc.d);
        std::cerr << "--- C++ Debug End ---" << std::endl;
        std::cerr << "Result from C++ debug: " << result_debug << std::endl;

        // --- Call Original C++ function for comparison ---
        uint64_t result_original = fastmod::fastmod_u64(tc.a, M_correct, tc.d);
        std::cerr << "Result from C++ original: " << result_original << std::endl;

        // --- Compare Debug vs Original ---
        if (result_debug == result_original) {
            std::cerr << "C++ Debug == C++ Original: MATCH" << std::endl;
        } else {
            std::cerr << "C++ Debug != C++ Original: MISMATCH" << std::endl;
            overall_pass = false; // Fail the test if debug and original don't match
        }

        // --- Compare Debug vs Expected ---
        bool case_passed = (result_debug == tc.expected_result);
        if (case_passed) {
            std::cerr << "C++ Debug PASSED vs Expected!" << std::endl;
        } else {
            std::cerr << "C++ Debug FAILED vs Expected (Expected: " << tc.expected_result << ")" << std::endl;
            overall_pass = false;
        }
    }

    std::cerr << "\n--- Test Summary ---" << std::endl;
    if (overall_pass) {
        std::cerr << "Test PASSED: C++ debug version matches expected results." << std::endl;
        return 0;
    } else {
        std::cerr << "Test FAILED: C++ debug version did NOT match expected results." << std::endl;
        return 1;
    }
}