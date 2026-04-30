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
    // Phase 6.0: Developer-Level Scanner Override
    // Hardcode root to external storage and skip internal data folders
    std::string final_root = "/storage/emulated/0/"; 

    LOGI(TAG, "Background scan queued. Waiting for database readiness...");
    
    // Phase 5.3: Block until LTM is hydrated
    while (!m_db_ready.load() && !m_stop_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    if (m_stop_requested.load()) return;

    LOGI(TAG, "Background scan started: %s (Override: /storage/emulated/0/)", final_root.c_str());
    int indexed_count = 0;

    try {
        if (!fs::exists(final_root)) {
            LOGE(TAG, "Root path does not exist: %s", final_root.c_str());
            m_is_running.store(false);
            return;
        }

        // Use recursive_directory_iterator for C++20 filesystem traversal
        auto options = fs::directory_options::skip_permission_denied;
        for (const auto& entry : fs::recursive_directory_iterator(final_root, options)) {
            if (m_stop_requested.load()) break;

            // --- Thermal Awareness ---
            while (Ronin::Kernel::Intent::g_thermal_state == Ronin::Kernel::Intent::ThermalState::SEVERE) {
                if (m_stop_requested.load()) break;
                LOGI(TAG, "Thermal SEVERE: Pausing file scanner...");
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }

            if (entry.is_regular_file()) {
                const auto& path = entry.path();
                std::string abs_path = fs::absolute(path).string();
                
                // Requirement 2: Internal Exclusion Logic
                // Skip anything in /data/ (app internal storage) to prevent indexing Ronin's own logs/db
                if (abs_path.find("/data/") != std::string::npos) {
                    continue;
                }

                std::string filename = path.filename().string();
                std::string extension = path.extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
                
                // Requirement 3: Extension Whitelist (Dev-Tools)
                // Only index files that are relevant for a Developer Assistant
                bool is_whitelisted = (extension == ".md" || extension == ".json" || 
                                     extension == ".yml" || extension == ".yaml" || 
                                     extension == ".zig" || extension == ".py" ||
                                     extension == ".cpp" || extension == ".h" ||
                                     extension == ".txt");

                if (!is_whitelisted) {
                    continue;
                }

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
                    if (indexed_count % 10 == 0) {
                        LOGI(TAG, "Indexing progress: %d files...", indexed_count);
                    }
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
