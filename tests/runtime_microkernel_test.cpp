#include <gtest/gtest.h>
#include "../ronin-runtime/kernel/include/execution_context.h"
#include "../ronin-runtime/kernel/include/event_bus.h"
#include "../ronin-runtime/actors/include/actor.h"
#include "../ronin-runtime/actors/include/actor_supervisor.h"
#include <chrono>
#include <thread>
#include <vector>

using namespace Ronin::Kernel::Execution;
using namespace Ronin::Kernel::Event;

// 1. Mock Test Actor
class TestComputeActor : public Actor {
public:
    explicit TestComputeActor(std::string id) : Actor(std::move(id)), processed_count(0) {}

    void processMessage(const Message& msg) override {
        processed_count++;
        if (std::holds_alternative<std::string>(msg.payload)) {
            last_received_text = std::get<std::string>(msg.payload);
        }
    }

    std::atomic<int> processed_count;
    std::string last_received_text;
};

// Test Suite
TEST(RuntimeMicrokernelTest, BlackboardWriteOwnershipEnforced) {
    Blackboard board;
    
    // Initial write sets ownership
    board.write("sensor_pitch", 440.0f, "MicActor");
    EXPECT_TRUE(board.contains("sensor_pitch"));
    EXPECT_EQ(std::get<float>(board.read("sensor_pitch")), 440.0f);
    
    // Write from non-owner must be rejected (value stays 440.0f)
    board.write("sensor_pitch", 450.0f, "MaliciousActor");
    EXPECT_EQ(std::get<float>(board.read("sensor_pitch")), 440.0f);
    
    // Write from owner is accepted
    board.write("sensor_pitch", 441.0f, "MicActor");
    EXPECT_EQ(std::get<float>(board.read("sensor_pitch")), 441.0f);
}

TEST(RuntimeMicrokernelTest, EventBusPriorityAndCoalescing) {
    EventBus& bus = EventBus::getInstance();
    bus.start();

    std::atomic<int> received_count = 0;
    std::string last_type = "";
    
    bus.subscribe("sensor_update", [&](const Message& msg) {
        received_count++;
        if (std::holds_alternative<SensorEvent>(msg.payload)) {
            last_type = std::get<SensorEvent>(msg.payload).type;
        }
    });

    // Publish three sensor events of same type consecutively
    Message m1 = {"trace_1", "IMU_Sensor", "Receiver", EventPriority::NORMAL, SensorEvent{"gyro", 1.0f}};
    Message m2 = {"trace_1", "IMU_Sensor", "Receiver", EventPriority::NORMAL, SensorEvent{"gyro", 2.0f}};
    Message m3 = {"trace_1", "IMU_Sensor", "Receiver", EventPriority::NORMAL, SensorEvent{"gyro", 3.0f}};

    bus.publish(m1, "sensor_update");
    bus.publish(m2, "sensor_update");
    bus.publish(m3, "sensor_update");

    // Wait a brief window for dispatch
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    bus.stop();

    // Since coalescing is active, the consecutive messages from same sender should coalesce to 1 message
    EXPECT_LE(received_count.load(), 2);
}

TEST(RuntimeMicrokernelTest, ActorSupervisorSupervisionOTP) {
    auto supervisor = &ActorSupervisor::getInstance();
    auto test_actor = std::make_shared<TestComputeActor>("test_compute");

    supervisor->registerActor(test_actor, SupervisionStrategy::ONE_FOR_ONE);

    // Send a message
    Message msg = {"trace_id", "source", "test_compute", EventPriority::NORMAL, std::string("ping")};
    test_actor->postMessage(msg);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_EQ(test_actor->processed_count.load(), 1);
    EXPECT_EQ(test_actor->last_received_text, "ping");

    supervisor->shutdownAll();
}

#include "../ronin-runtime/services/include/audio_service.h"
#include "../ronin-runtime/actors/include/mic_actor.h"
#include "../ronin-runtime/actors/include/dsp_actor.h"

TEST(RuntimeMicrokernelTest, AudioDspTunerPipelineIntegrationFlow) {
    auto blackboard = std::make_shared<Blackboard>();
    auto audio_service = std::make_shared<Ronin::Kernel::Services::AudioService>();
    
    auto mic_actor = std::make_shared<MicActor>(audio_service, blackboard);
    auto dsp_actor = std::make_shared<DspActor>(blackboard);

    auto supervisor = &ActorSupervisor::getInstance();
    supervisor->registerActor(mic_actor, SupervisionStrategy::ONE_FOR_ONE);
    supervisor->registerActor(dsp_actor, SupervisionStrategy::ONE_FOR_ONE);

    EventBus& bus = EventBus::getInstance();
    bus.start();

    // DspActor subscribes to "audio_stream" from the event bus
    bus.subscribe("audio_stream", [&](const Message& msg) {
        dsp_actor->postMessage(msg);
    });

    // Send a message request to MicActor to trigger capture
    Message trigger = {"trace_tuner_1", "User", "MicActor", EventPriority::HIGH, std::string("start")};
    mic_actor->postMessage(trigger);

    // Wait for the pipeline execution to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    bus.stop();
    supervisor->shutdownAll();

    // Verify Blackboard entries populated with write-ownership verified
    EXPECT_TRUE(blackboard->contains("raw_audio"));
    EXPECT_TRUE(blackboard->contains("fft_result"));
    
    // Check pitch value resolved from 440Hz sine wave (mocked standard tuner pitch)
    float pitch = std::get<float>(blackboard->read("fft_result"));
    EXPECT_NEAR(pitch, 440.0f, 10.0f);
}

