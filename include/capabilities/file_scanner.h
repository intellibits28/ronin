#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <filesystem>
#include "long_term_memory.h"
#include "neural_embedding_node.h"

namespace Ronin::Kernel::Capability {

class FileScanner {
public:
    FileScanner(Memory::LongTermMemory& ltm, NeuralEmbeddingNode* neural = nullptr);
    ~FileScanner();

    // Start scanning /storage/emulated/0/ in a background thread
    void startScan(const std::string& root_path = "/storage/emulated/0");

    // Stop the background scan
    void stopScan();

    // Check if a scan is currently active
    bool isScanning() const { return m_is_running.load(); }

private:
    Memory::LongTermMemory& m_ltm;
    NeuralEmbeddingNode* m_neural;
    std::thread m_scan_thread;
    std::atomic<bool> m_is_running{false};
    std::atomic<bool> m_stop_requested{false};

    void scanWorker(const std::string& root_path);
};

} // namespace Ronin::Kernel::Capability
