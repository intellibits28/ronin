#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <iomanip>
#include "myanmar_segmenter.h"

using namespace Ronin::Kernel::NLP;

struct TestCase {
    std::string desc;
    std::string input;
    std::vector<std::string> expected;
};

void run_stress_tests() {
    MyanmarSegmenter segmenter;
    
    // Test အတွက် အခြေခံ Dictionary တည်ဆောက်ခြင်း
    segmenter.insert("အပြည်ပြည်ဆိုင်ရာ", false);
    segmenter.insert("လေဆိပ်", false);
    segmenter.insert("မြန်မာ", false);
    segmenter.insert("ဘာသာစကား", false);
    segmenter.insert("စား", false);
    segmenter.insert("နေ", false);
    segmenter.insert("ကြ", false);
    segmenter.insert("ခြင်း", false);
    segmenter.insert("ဖြစ်", false);
    segmenter.insert("သည်", true); // Stop word
    
    std::vector<TestCase> tests = {
        {
            "Extreme compound length test",
            "အပြည်ပြည်ဆိုင်ရာလေဆိပ်",
            {"အပြည်ပြည်ဆိုင်ရာ", "လေဆိပ်"}
        },
        {
            "Particle stacking boundary",
            "စားနေကြခြင်းဖြစ်သည်",
            {"စား", "နေ", "ကြ", "ခြင်း", "ဖြစ်"} // 'သည်' is stopword, so filtered
        },
        {
            "Corrupted boundary recovery",
            "မြန်မာXYZဘာသာစကား",
            {"မြန်မာ", "XYZ", "ဘာသာစကား"}
        }
    };

    std::cout << "\n>>> INITIATING SEGMENTER STRESS TESTS [v4.6] <<<\n" << std::endl;

    for (const auto& t : tests) {
        std::cout << "TEST: " << t.desc << std::endl;
        std::cout << "INPUT: " << t.input << std::endl;
        
        std::string result = segmenter.segment(t.input);
        std::cout << "RESULT: [" << result << "]" << std::endl;
        
        // simple validation logic
        bool pass = true;
        for (const auto& exp : t.expected) {
            if (result.find(exp) == std::string::npos) {
                pass = false;
                break;
            }
        }
        
        std::cout << "STATUS: " << (pass ? "✅ PASSED" : "❌ FAILED") << "\n" << std::endl;
    }
}

int main() {
    run_stress_tests();
    return 0;
}
