#include <gtest/gtest.h>
#include "semantic_router.h"
#include "intent_centroids.h"
#include "intent_engine.h"
#include "security/credential_sanitizer.h"
#include <cmath>

using namespace Ronin::Kernel::Intent;
using namespace Ronin::Kernel::Security;
using Ronin::Kernel::AgentPlan;

// ============================================================================
// 1. L2 Re-Normalization Tests (Architectural Refinement 1)
// ============================================================================

TEST(SemanticRoutingTest, L2DenseNormalizationStrictUnitLength) {
    std::vector<float> vec = {3.0f, 4.0f, 0.0f};
    l2Normalize(vec);
    
    float norm = 0.0f;
    for (float v : vec) norm += v * v;
    norm = std::sqrt(norm);

    EXPECT_NEAR(norm, 1.0f, 1e-5f);
    EXPECT_NEAR(vec[0], 0.6f, 1e-5f);
    EXPECT_NEAR(vec[1], 0.8f, 1e-5f);
    EXPECT_NEAR(vec[2], 0.0f, 1e-5f);
}

TEST(SemanticRoutingTest, CosineDotProductBetweenUnitVectors) {
    std::vector<float> a = {1.0f, 0.0f, 0.0f};
    std::vector<float> b = {0.0f, 1.0f, 0.0f};
    std::vector<float> c = {1.0f, 0.0f, 0.0f};

    EXPECT_NEAR(dotProductCosine(a, b), 0.0f, 1e-5f); // Orthogonal
    EXPECT_NEAR(dotProductCosine(a, c), 1.0f, 1e-5f); // Identical
}

TEST(SemanticRoutingTest, SparseVectorizerStrictUnitNormalization) {
    auto vec1 = SemanticRouter::vectorize("update my gemini api key to new key");
    EXPECT_NEAR(vec1.norm, 1.0f, 1e-5f);

    auto vec2 = SemanticRouter::vectorize("ဓာတ်မီး ဖွင့်ပေးပါ");
    EXPECT_NEAR(vec2.norm, 1.0f, 1e-5f);

    auto vec3 = SemanticRouter::vectorize("wifi ဖွင့်ပါ");
    EXPECT_NEAR(vec3.norm, 1.0f, 1e-5f);
}

// ============================================================================
// 2. Multi-Centroid Sub-Clustering & Max-Pooling Tests (Refinement 2)
// ============================================================================

TEST(SemanticRoutingTest, MultiCentroidMaxpoolingAcrossRegisters) {
    SemanticRouter router;

    // English Register
    auto match_en = router.route("show my gemini api key");
    EXPECT_TRUE(match_en.is_confident);
    EXPECT_EQ(match_en.route_name, "VAULT_LOOKUP");

    // Burmese Register
    auto match_mm = router.route("လျှို့ဝှက်ကုဒ် ပြပါ");
    EXPECT_TRUE(match_mm.is_confident);
    EXPECT_EQ(match_mm.route_name, "VAULT_LOOKUP");

    // Burm-glish Register
    auto match_mix = router.route("gemini api key ပြပါ");
    EXPECT_TRUE(match_mix.is_confident);
    EXPECT_EQ(match_mix.route_name, "VAULT_LOOKUP");
}

TEST(SemanticRoutingTest, FlashlightAndHardwareRouting) {
    SemanticRouter router;

    auto m1 = router.route("turn on flashlight");
    EXPECT_TRUE(m1.is_confident);
    EXPECT_EQ(m1.route_name, "FLASHLIGHT");

    auto m2 = router.route("မီးဖွင့်");
    EXPECT_TRUE(m2.is_confident);
    EXPECT_EQ(m2.route_name, "FLASHLIGHT");

    auto m3 = router.route("ဓာတ်မီး ပိတ်");
    EXPECT_TRUE(m3.is_confident);
    EXPECT_EQ(m3.route_name, "FLASHLIGHT");
}

// ============================================================================
// 3. Top-2 Margin Guard & Lexical Tie-Breaker Tests (Refinement 3)
// ============================================================================

TEST(SemanticRoutingTest, LexicalTieBreakerResolvesMutationVsSaveVsLookup) {
    std::string resolved;

    // Test Mutation Marker
    bool b1 = SemanticRouter::performLexicalTieBreak("gemini api key အသစ် ပြောင်းပါ", "VAULT_SAVE", "VAULT_UPDATE", resolved);
    EXPECT_TRUE(b1);
    EXPECT_EQ(resolved, "VAULT_UPDATE");

    bool b2 = SemanticRouter::performLexicalTieBreak("update gemini api key", "VAULT_SAVE", "VAULT_UPDATE", resolved);
    EXPECT_TRUE(b2);
    EXPECT_EQ(resolved, "VAULT_UPDATE");

    // Test Save Marker
    bool b3 = SemanticRouter::performLexicalTieBreak("gemini api key သိမ်းပါ", "VAULT_UPDATE", "VAULT_SAVE", resolved);
    EXPECT_TRUE(b3);
    EXPECT_EQ(resolved, "VAULT_SAVE");

    // Test Lookup Marker
    bool b4 = SemanticRouter::performLexicalTieBreak("gemini api key ပြပါ", "VAULT_SAVE", "VAULT_LOOKUP", resolved);
    EXPECT_TRUE(b4);
    EXPECT_EQ(resolved, "VAULT_LOOKUP");
}

TEST(SemanticRoutingTest, AmbiguousQueryRoutesToClarification) {
    SemanticRouter router;
    
    // A query with equal scores or ambiguous intent without tie-breaking cues
    // Testing the tie-breaker directly when neither cue is present
    std::string resolved;
    bool b = SemanticRouter::performLexicalTieBreak("something completely vague", "VAULT_SAVE", "VAULT_UPDATE", resolved);
    EXPECT_FALSE(b);
}

// ============================================================================
// 4. Value-Based Credential Redaction Scanner Tests (Refinement 4)
// ============================================================================

TEST(CredentialSanitizerTest, RedactsGoogleApiKey) {
    std::string input = "Update key to AIzaSyD1234567890abcdefghijklmnopqrst in vault";
    std::string sanitized = CredentialSanitizer::sanitizeText(input);

    EXPECT_EQ(sanitized.find("AIzaSyD1234567890abcdefghijklmnopqrst"), std::string::npos);
    EXPECT_NE(sanitized.find("[REDACTED_SECRET]"), std::string::npos);
}

TEST(CredentialSanitizerTest, RedactsOpenAiAndAnthropicKey) {
    std::string input = "Set OpenAI token sk-1234567890abcdefghijklmnopqrstuv and proceed";
    std::string sanitized = CredentialSanitizer::sanitizeText(input);

    EXPECT_EQ(sanitized.find("sk-1234567890abcdefghijklmnopqrstuv"), std::string::npos);
    EXPECT_NE(sanitized.find("[REDACTED_SECRET]"), std::string::npos);
}

TEST(CredentialSanitizerTest, RedactsGitHubTokens) {
    std::string input = "Sync repo with ghp_123456789012345678901234567890123456";
    std::string sanitized = CredentialSanitizer::sanitizeText(input);

    EXPECT_EQ(sanitized.find("ghp_123456789012345678901234567890123456"), std::string::npos);
    EXPECT_NE(sanitized.find("[REDACTED_SECRET]"), std::string::npos);
}

TEST(CredentialSanitizerTest, RedactsBearerHeaders) {
    std::string input = "Authorization: Bearer mySecretTokenPayload12345";
    std::string sanitized = CredentialSanitizer::sanitizeText(input);

    EXPECT_EQ(sanitized.find("mySecretTokenPayload12345"), std::string::npos);
    EXPECT_NE(sanitized.find("[REDACTED_SECRET]"), std::string::npos);
}

TEST(CredentialSanitizerTest, RedactsBurmeseAssignmentMarkers) {
    std::string input = "gemini api key ကို AIzaSyD1234567890abcdefghijklmnopqrst အဖြစ် ပြောင်းပေးပါ";
    std::string sanitized = CredentialSanitizer::sanitizeText(input);

    EXPECT_EQ(sanitized.find("AIzaSyD1234567890abcdefghijklmnopqrst"), std::string::npos);
    EXPECT_NE(sanitized.find("[REDACTED_SECRET]"), std::string::npos);
}

TEST(CredentialSanitizerTest, RedactsJsonSensitiveKeysAndValues) {
    std::string json_input = R"({
        "intent": "VAULT_UPDATE",
        "vault_title": "gemini api key",
        "vault_content": "AIzaSyD1234567890abcdefghijklmnopqrst",
        "query": "update key to AIzaSyD1234567890abcdefghijklmnopqrst"
    })";

    std::string sanitized = CredentialSanitizer::sanitizeJson(json_input);

    EXPECT_EQ(sanitized.find("AIzaSyD1234567890abcdefghijklmnopqrst"), std::string::npos);
    EXPECT_NE(sanitized.find("[REDACTED_SECRET]"), std::string::npos);
}

// ============================================================================
// 5. Stateful Multi-Turn Context Carry-Over Tests (Refinement 5)
// ============================================================================

TEST(SemanticRoutingTest, MultiTurnClarificationContextCarryOver) {
    IntentEngine engine;
    auto* planner = engine.getPlanner();
    ASSERT_NE(planner, nullptr);

    // Turn 1: User requests update without providing new value
    AgentPlan plan1;
    bool fastpath1 = planner->tryFastPathRoute("update my gemini api key", plan1);
    EXPECT_TRUE(fastpath1);
    EXPECT_EQ(plan1.intent_name, "fallback_chat");
    EXPECT_TRUE(planner->hasPendingSlot());
    EXPECT_EQ(planner->getPendingSlot().pending_intent, "UPDATE_VAULT");
    EXPECT_EQ(planner->getPendingSlot().target_entity, "gemini api key");

    // Turn 2: User provides new secret key in follow-up
    AgentPlan plan2;
    std::string secret_token = "AIzaSyD1234567890abcdefghijklmnopqrst";
    bool fastpath2 = planner->tryFastPathRoute(secret_token, plan2);
    
    EXPECT_TRUE(fastpath2);
    EXPECT_EQ(plan2.intent_name, "UPDATE_VAULT");
    EXPECT_EQ(plan2.parameters["vault_title"], "gemini api key");
    EXPECT_EQ(plan2.parameters["vault_content"], secret_token);
    
    // Slot should now be consumed and cleared
    EXPECT_FALSE(planner->hasPendingSlot());
}

TEST(SemanticRoutingTest, MultiTurnClarificationCancellation) {
    IntentEngine engine;
    auto* planner = engine.getPlanner();
    ASSERT_NE(planner, nullptr);

    // Turn 1: User requests update without value
    AgentPlan plan1;
    planner->tryFastPathRoute("gemini api key ပြင်ပေးပါ", plan1);
    EXPECT_TRUE(planner->hasPendingSlot());

    // Turn 2: User cancels
    AgentPlan plan2;
    planner->tryFastPathRoute("မလုပ်တော့ဘူး", plan2);
    
    // Slot should be cleared on cancellation
    EXPECT_FALSE(planner->hasPendingSlot());
}
