#include "ronin_kernel.h"
#include "ronin_log.h"

#define TAG "RoninKernel"

namespace Ronin::Kernel {

RoninKernel::RoninKernel(
    Intent::IntentEngine& intent_engine,
    Reasoning::CapabilityGraph& graph,
    Reasoning::GraphExecutor& executor,
    Memory::MemoryManager& memory,
    Checkpoint::CheckpointEngine& checkpoint
) : m_intent_engine(intent_engine), 
    m_graph(graph), 
    m_executor(executor), 
    m_memory(memory), 
    m_checkpoint(checkpoint) {}

void RoninKernel::tick(const std::string& input) {
    // 1. Determine Intent
    m_state.intent = m_intent_engine.process(input);

    // 2. Resolve Plan (Find capable node)
    // Map 'resolve' to selecting the next node based on input/intent
    auto plan = m_executor.selectNextNode(input);
    
    if (plan == nullptr) {
        LOGW(TAG, "> Warning: No capable node found for intent.");
        return;
    }

    m_state.active_node = plan;

    // 3. Execute Plan
    // Map 'execute' to actual routing or logic inside executor
    // For now, selectNextNode already did the selection. 
    // In a full implementation, this might run node->execute()
    LOGI(TAG, "Heartbeat: Executing node '%s'", plan->capability_name.c_str());

    // 4. Update Memory
    // Map 'update' to adding token or updating context
    Memory::Token t = {plan->id, 1.0f, {m_state.intent}};
    m_memory.addRecentToken(t);

    // 5. Save Checkpoint
    m_checkpoint.persistToStorage();
    
    LOGI(TAG, "Heartbeat tick complete. [Kernel v3.6-CORE-TICK]");
}

} // namespace Ronin::Kernel
