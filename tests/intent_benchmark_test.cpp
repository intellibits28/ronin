#include <gtest/gtest.h>
#include "myanmar_linguistic_normalizer.h"
#include "intent_slot_extractors.h"
#include "semantic_router.h"
#include "intent_engine.h"
#include <chrono>
#include <iostream>

using namespace Ronin::Kernel::NLP;
using namespace Ronin::Kernel::Intent;

// ============================================================================
// 1. Myanmar Linguistic Normalizer Tests
// ============================================================================
TEST(MyanmarNormalizerTest, DigitAndNumberNormalization) {
    std::string burmese_digits = "၀၉၁၂၃၄၅၆၇၈";
    std::string normalized_digits = MyanmarLinguisticNormalizer::normalizeDigits(burmese_digits);
    EXPECT_EQ(normalized_digits, "0912345678");

    std::string time_text = "မနက် ၇ နာရီခွဲ";
    auto res = MyanmarLinguisticNormalizer::normalize(time_text);
    EXPECT_NE(res.raw_normalized_text.find("7:30"), std::string::npos);

    std::string written_num = "၃ နာရီ";
    auto res_num = MyanmarLinguisticNormalizer::normalize(written_num);
    EXPECT_NE(res_num.raw_normalized_text.find("3:00"), std::string::npos);
}

TEST(MyanmarNormalizerTest, PoliteAndQuestionParticleStripping) {
    bool is_q = false;
    std::string clean1 = MyanmarLinguisticNormalizer::stripPoliteParticles("ဓာတ်မီး ဖွင့်ပေးပါဦး", is_q);
    EXPECT_EQ(clean1, "ဓာတ်မီး ဖွင့်");
    EXPECT_FALSE(is_q);

    std::string clean2 = MyanmarLinguisticNormalizer::stripPoliteParticles("ဂစ်တာ အသံညှိချင်တယ်", is_q);
    EXPECT_EQ(clean2, "ဂစ်တာ အသံညှိ");
    EXPECT_FALSE(is_q);

    std::string clean3 = MyanmarLinguisticNormalizer::stripPoliteParticles("မင်း ဘာတွေလုပ်နိုင်သလဲ?", is_q);
    EXPECT_TRUE(is_q);
    EXPECT_NE(clean3.find("မင်း ဘာတွေလုပ်နိုင်"), std::string::npos);

    std::string clean4 = MyanmarLinguisticNormalizer::stripPoliteParticles("တုန်ခါမှု စစ်ဆေးပေးပါရစေဦး", is_q);
    EXPECT_EQ(clean4, "တုန်ခါမှု စစ်ဆေး");
}

// ============================================================================
// 2. Intent Slot Extractors Tests
// ============================================================================
TEST(SlotExtractorTest, SmsSlots) {
    auto slots1 = IntentSlotExtractors::extractSmsSlots("မသိမ့် ဆီ sms ပို့ပါ");
    EXPECT_TRUE(slots1.is_valid);
    EXPECT_EQ(slots1.recipient, "မသိမ့်");
    EXPECT_FALSE(slots1.attach_location);

    auto slots2 = IntentSlotExtractors::extractSmsSlots("send message to John with my location");
    EXPECT_TRUE(slots2.is_valid);
    EXPECT_EQ(slots2.recipient, "john");
    EXPECT_TRUE(slots2.attach_location);

    auto slots3 = IntentSlotExtractors::extractSmsSlots("0912345678 ကို \"နေကောင်းလား\" လို့ စာပို့ပေးပါ");
    EXPECT_TRUE(slots3.is_valid);
    EXPECT_EQ(slots3.recipient, "0912345678");
    EXPECT_EQ(slots3.message, "နေကောင်းလား");
}

TEST(SlotExtractorTest, AlarmSlots) {
    auto slots1 = IntentSlotExtractors::extractAlarmSlots("မနက်ဖြန် မနက် ၇ နာရီ နှိုးပါ");
    EXPECT_TRUE(slots1.is_valid);
    EXPECT_EQ(slots1.date_str, "tomorrow");
    EXPECT_EQ(slots1.time_str, "07:00");

    auto slots2 = IntentSlotExtractors::extractAlarmSlots("ညနေ ၅ နာရီခွဲ နှိုးစက်ပေး");
    EXPECT_TRUE(slots2.is_valid);
    EXPECT_EQ(slots2.time_str, "17:30");
}

TEST(SlotExtractorTest, VaultSlots) {
    auto slots1 = IntentSlotExtractors::extractVaultSlots("ကားနံပါတ် 123 vault ထဲသိမ်းပါ");
    EXPECT_TRUE(slots1.is_valid);
    EXPECT_TRUE(slots1.is_save);
    EXPECT_NE(slots1.title.find("ကားနံပါတ်"), std::string::npos);

    auto slots2 = IntentSlotExtractors::extractVaultSlots("gemini api key ရှာပေးပါ");
    EXPECT_TRUE(slots2.is_valid);
    EXPECT_FALSE(slots2.is_save);
    EXPECT_NE(slots2.title.find("gemini api key"), std::string::npos);
}

TEST(SlotExtractorTest, FileSearchSlots) {
    auto slots1 = IntentSlotExtractors::extractFileSearchSlots("invoice pdf ဖိုင်တွေ ရှာပေးပါ");
    EXPECT_TRUE(slots1.is_valid);
    EXPECT_EQ(slots1.file_extension, "pdf");

    auto slots2 = IntentSlotExtractors::extractFileSearchSlots("find report docx files");
    EXPECT_TRUE(slots2.is_valid);
    EXPECT_EQ(slots2.file_extension, "docx");
}

TEST(SlotExtractorTest, LocationSlots) {
    auto slots1 = IntentSlotExtractors::extractLocationSlots("ငါ့ အိမ် တည်နေရာ မှတ်ထားပါ");
    EXPECT_TRUE(slots1.is_saving);
    EXPECT_EQ(slots1.entity, "Home");

    auto slots2 = IntentSlotExtractors::extractLocationSlots("ရန်ကုန် မြေပုံ ဖွင့်ပါ");
    EXPECT_TRUE(slots2.is_map_open);
}

// ============================================================================
// 3. Semantic Router Benchmark Tests
// ============================================================================
TEST(SemanticRouterBenchmarkTest, ComprehensiveRoutingAccuracy) {
    SemanticRouter router;

    struct TestCase {
        std::string query;
        std::string expected_route;
    };

    std::vector<TestCase> test_cases = {
        // Flashlight
        {"turn on flashlight", "FLASHLIGHT"},
        {"switch off flashlight", "FLASHLIGHT"},
        {"ဓာတ်မီး ဖွင့်ပေးပါ", "FLASHLIGHT"},
        {"မီးပိတ်လိုက်တော့", "FLASHLIGHT"},
        {"ဓာတ်မီး ထိုးပေး", "FLASHLIGHT"},

        // Guitar & Instrument Tuner
        {"guitar tuner", "PITCH_ANALYSIS"},
        {"tune my guitar", "PITCH_ANALYSIS"},
        {"run guitar tuner", "PITCH_ANALYSIS"},
        {"ဂစ်တာ အသံညှိပေးပါ", "PITCH_ANALYSIS"},
        {"ဂစ်တာ ကြိုးညှိမယ်", "PITCH_ANALYSIS"},
        {"တူရိယာ အသံညှိ", "PITCH_ANALYSIS"},

        // SHM Vibration
        {"analyze vibration", "SHM_VIBRATION"},
        {"check structural health", "SHM_VIBRATION"},
        {"building resonance test", "SHM_VIBRATION"},
        {"တုန်ခါမှု စစ်ဆေးပေးပါ", "SHM_VIBRATION"},
        {"အဆောက်အအုံ ကြံ့ခိုင်မှု တိုင်းတာပါ", "SHM_VIBRATION"},

        // Location
        {"where am I", "LOCATION"},
        {"show my location", "LOCATION"},
        {"ငါ အခု ဘယ်ရောက်နေလဲ", "LOCATION"},
        {"တည်နေရာ ပြပေးပါ", "LOCATION"},
        {"မြေပုံ ဖွင့်ပါ", "LOCATION"},

        // Device Settings
        {"turn on wifi", "DEVICE_SETTINGS"},
        {"turn off bluetooth", "DEVICE_SETTINGS"},
        {"ဝိုင်ဖိုင် ဖွင့်ပါ", "DEVICE_SETTINGS"},
        {"ဘလူးတု ပိတ်ပေး", "DEVICE_SETTINGS"},

        // File Search
        {"find pdf files", "FILE_SEARCH"},
        {"search documents", "FILE_SEARCH"},
        {"ဖိုင် ရှာပေးပါ", "FILE_SEARCH"},
        {"စာရွက်စာတမ်း ရှာပါ", "FILE_SEARCH"},

        // SMS
        {"send sms to brother", "SEND_SMS"},
        {"မက်ဆေ့ခ်ျ ပို့ပေးပါ", "SEND_SMS"},
        {"0912345678 ဆီ စာပို့ပါ", "SEND_SMS"},

        // Alarm
        {"set alarm at 7am", "SET_ALARM"},
        {"wake me up tomorrow", "SET_ALARM"},
        {"မနက်ဖြန် ၇ နာရီ နှိုးပါ", "SET_ALARM"},
        {"နှိုးစက် ပေးပါ", "SET_ALARM"},

        // Vault
        {"save password to vault", "VAULT_MEMORY"},
        {"မှတ်ထားပေးပါ", "VAULT_MEMORY"},
        {"လျှို့ဝှက်ချက် သိမ်းထား", "VAULT_MEMORY"},

        // General Chat
        {"hello ronin", "CHAT_QUERY"},
        {"မင်္ဂလာပါ", "CHAT_QUERY"},
        {"နေကောင်းလား", "CHAT_QUERY"},
        {"မင်း ဘာတွေလုပ်နိုင်လဲ", "CHAT_QUERY"}
    };

    int passed = 0;
    auto start_time = std::chrono::high_resolution_clock::now();

    for (const auto& tc : test_cases) {
        auto match = router.route(tc.query);
        if (match.route_name == tc.expected_route && match.is_confident) {
            passed++;
        } else {
            std::cout << "[MISMATCH] Query: '" << tc.query << "' -> got: " 
                      << match.route_name << " (conf: " << match.confidence 
                      << ", confident: " << match.is_confident << "), expected: " 
                      << tc.expected_route << std::endl;
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    double avg_us = (total_ms * 1000.0) / test_cases.size();

    std::cout << "\n======================================================\n";
    std::cout << "  SEMANTIC ROUTER BENCHMARK RESULTS\n";
    std::cout << "  Total Queries:  " << test_cases.size() << "\n";
    std::cout << "  Passed:         " << passed << " / " << test_cases.size() 
              << " (" << (passed * 100.0 / test_cases.size()) << "%)\n";
    std::cout << "  Total Latency:  " << total_ms << " ms\n";
    std::cout << "  Avg Latency:    " << avg_us << " microseconds/query\n";
    std::cout << "======================================================\n";

    EXPECT_GE(passed, static_cast<int>(test_cases.size() * 0.95)); // >= 95% accuracy
    EXPECT_LT(avg_us, 500.0); // Sub-millisecond latency (< 500 us)
}
