#include <gtest/gtest.h>
#include "capabilities/hardware_bridge.h"
#include "models/inference_engine.h"
#include "ronin_log.h"
#include <string>

using namespace Ronin::Kernel::Capability;

TEST(CloudProviderRegressionTest, HelloRoninInferenceTest) {
    std::string input = "Hello Ronin";
    std::string provider = "Gemini";
    std::string apiKey = "";

    // Test fetchCloudResponse regression ("Hello Ronin")
    std::string response = HardwareBridge::fetchCloudResponse(input, provider, apiKey);
    EXPECT_FALSE(response.empty());
    
    // In host build, expect mock response; in target build with valid setup, expect HTTP 200 non-empty response
#ifndef __ANDROID__
    EXPECT_EQ(response, "Host Build: Cloud response mocked.");
#else
    EXPECT_NE(response, "Error: Method fetchCloudResponse failed.");
#endif
}

TEST(CloudProviderRegressionTest, ProviderHealthCheckTest) {
    std::string provider = "Gemini";
    std::string response = HardwareBridge::checkProviderHealth(provider, "");
    EXPECT_FALSE(response.empty());

#ifndef __ANDROID__
    EXPECT_EQ(response, "Host Build: Provider health check mocked successfully.");
#else
    EXPECT_NE(response, "Error: Method checkProviderHealth failed.");
#endif
}
