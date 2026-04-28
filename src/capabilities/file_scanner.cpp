#include "capabilities/file_scanner.h"
#include "intent_engine.h"
#include "ronin_log.h"
#include <chrono>

#define TAG "RoninFileScanner"

namespace Ronin::Kernel::Capability {

namespace fs = std::filesystem;

FileScanner::FileScanner(Memory::LongTermMemory& ltm, NeuralEmbeddingNode* neural) 
    : m_ltm(ltm), m_neural(neural) {}

FileScanner::~FileScanner() {
    stopScan();
}

void FileScanner::startScan(const std::string& root_path) {
    if (m_is_running.exchange(true)) {
        LOGI(TAG, "Scan already in progress. Skipping start request.");
        return;
    }

    m_stop_requested.store(false);
    m_scan_thread = std::thread(&FileScanner::scanWorker, this, root_path);
}

void FileScanner::stopScan() {
    m_stop_requested.store(true);
    if (m_scan_thread.joinable()) {
        m_scan_thread.join();
    }
    m_is_running.store(false);
}

void FileScanner::scanWorker(const std::string& root_path) {
    LOGI(TAG, "Background scan queued. Waiting for database readiness...");
    
    // Phase 5.3: Block until LTM is hydrated
    while (!m_db_ready.load() && !m_stop_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    if (m_stop_requested.load()) return;

    LOGI(TAG, "Background scan started: %s", root_path.c_str());
    int indexed_count = 0;

    try {
        if (!fs::exists(root_path)) {
            LOGE(TAG, "Root path does not exist: %s", root_path.c_str());
            m_is_running.store(false);
            return;
        }

        // Use recursive_directory_iterator for C++20 filesystem traversal
        auto options = fs::directory_options::skip_permission_denied;
        for (const auto& entry : fs::recursive_directory_iterator(root_path, options)) {
            if (m_stop_requested.load()) break;

            // --- Thermal Awareness ---
            // Pause if thermal state is SEVERE to prevent device overheating
            while (Ronin::Kernel::Intent::g_thermal_state == Ronin::Kernel::Intent::ThermalState::SEVERE) {
                if (m_stop_requested.load()) break;
                LOGI(TAG, "Thermal SEVERE: Pausing file scanner...");
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }

            if (entry.is_regular_file()) {
                const auto& path = entry.path();
                std::string filename = path.filename().string();
                
                // Phase 5.3: Strict Extension Hard-Filter (No Images, No Noise)
                if (filename.find(".nomedia") != std::string::npos || 
                    filename.find(".db") != std::string::npos || 
                    filename.find(".uuid") != std::string::npos ||
                    filename.find(".database_uuid") != std::string::npos ||
                    filename.find(".jpg") != std::string::npos ||
                    filename.find(".jpeg") != std::string::npos ||
                    filename.find(".png") != std::string::npos ||
                    filename.find(".gif") != std::string::npos ||
                    filename.find(".ini") != std::string::npos ||
                    filename.find(".DS_Store") != std::string::npos) {
                    continue; 
                }

                std::string abs_path = fs::absolute(path).string();
                std::string extension = path.extension().string();
                
                auto ftime = fs::last_write_time(path);
                auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
                uint64_t modified = static_cast<uint64_t>(sctp.time_since_epoch().count());

                // Generate embedding if node is available
                std::vector<float> embedding;
                if (m_neural) {
                    embedding = m_neural->generateEmbedding(filename);
                }

                // Index into L3 Deep-store
                if (m_ltm.indexFile(filename, abs_path, extension, modified, embedding)) {
                    indexed_count++;
                }
            }

            // Yield CPU to maintain low-priority background operation
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    } catch (const std::exception& e) {
        LOGE(TAG, "Exception during file scan: %s", e.what());
    }

    LOGI(TAG, "Background scan completed. Indexed %d new files into SQLite.", indexed_count);
    m_is_running.store(false);
}

} // namespace Ronin::Kernel::Capability
