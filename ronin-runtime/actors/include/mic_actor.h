#pragma once

#include "actor.h"
#include "services/include/audio_service.h"
#include "kernel/include/execution_context.h"
#include <iostream>

namespace Ronin::Kernel::Execution {

class MicActor : public Actor {
public:
    MicActor(std::shared_ptr<Services::AudioService> audio_service, std::shared_ptr<Blackboard> blackboard)
        : Actor("MicActor"), m_audio_service(audio_service), m_blackboard(blackboard) {}

    void processMessage(const Event::Message& msg) override {
        if (std::holds_alternative<Event::LocationRequest>(msg.payload)) return; // Filter
        
        // Setup execution contexts
        ExecutionContext ctx;
        ctx.goal_id = "guitar_tuner_goal";
        ctx.trace_id = msg.trace_id;

        Services::ServiceRequest req;
        req.action = "capture_audio";

        // Dispatch async service execution
        auto handle = m_audio_service->execute(req, ctx);
        auto result = handle->await();

        if (result.success) {
            try {
                auto j = nlohmann::json::parse(result.result_json);
                std::vector<float> samples = j["array"].get<std::vector<float>>();
                
                // Write captured samples to Blackboard with owner actor check
                m_blackboard->write("raw_audio", samples, getId());

                // Publish AudioChunk event back to EventBus
                Event::Message outgoing;
                outgoing.trace_id = msg.trace_id;
                outgoing.sender_id = getId();
                outgoing.recipient_id = "DspActor";
                outgoing.priority = Event::EventPriority::HIGH;
                outgoing.payload = Event::AudioChunk{samples};

                Event::EventBus::getInstance().publish(outgoing, "audio_stream");
            } catch (...) {
                std::cerr << "[MicActor] JSON parse error during audio capture processing." << std::endl;
            }
        }
    }

private:
    std::shared_ptr<Services::AudioService> m_audio_service;
    std::shared_ptr<Blackboard> m_blackboard;
};

} // namespace Ronin::Kernel::Execution
