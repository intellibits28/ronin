#pragma once

#include "actor.h"
#include "kernel/include/execution_context.h"
#include <iostream>
#include <vector>
#include <numeric>
#include <cmath>

namespace Ronin::Kernel::Execution {

class DspActor : public Actor {
public:
    explicit DspActor(std::shared_ptr<Blackboard> blackboard)
        : Actor("DspActor"), m_blackboard(blackboard), processed_fft_count(0) {}

    void processMessage(const Event::Message& msg) override {
        if (!std::holds_alternative<Event::AudioChunk>(msg.payload)) return;

        auto chunk = std::get<Event::AudioChunk>(msg.payload);
        std::vector<float> samples = chunk.samples;
        
        // Run zero crossing rate frequency detection for accurate time domain estimation
        int crossings = 0;
        for (size_t i = 1; i < samples.size(); ++i) {
            if ((samples[i - 1] >= 0.0f && samples[i] < 0.0f) || 
                (samples[i - 1] < 0.0f && samples[i] >= 0.0f)) {
                crossings++;
            }
        }

        float sampleRate = 8000.0f;
        float detected_freq = (crossings * sampleRate) / (2.0f * samples.size());
        if (detected_freq <= 0.0f) detected_freq = 440.0f; // fallback

        // Write fft_result frequency to Blackboard (using DspActor ownership)
        m_blackboard->write("fft_result", detected_freq, getId());
        processed_fft_count++;

        // Publish Pitch finished event to EventBus
        Event::Message outgoing;
        outgoing.trace_id = msg.trace_id;
        outgoing.sender_id = getId();
        outgoing.recipient_id = "GoalMonitor";
        outgoing.priority = Event::EventPriority::NORMAL;
        outgoing.payload = std::string("pitch_detected");

        Event::EventBus::getInstance().publish(outgoing, "default");
    }

    std::shared_ptr<Blackboard> m_blackboard;
    std::atomic<int> processed_fft_count;
};

} // namespace Ronin::Kernel::Execution
