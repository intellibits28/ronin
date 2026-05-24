#include "capabilities/file_scanner.h"
#include "intent_engine.h"
#include "ronin_log.h"
#include <chrono>
#include <filesystem>
#include <algorithm>

#define TAG "RoninFileScanner"

namespace Ronin::Kernel::Capability {

namespace fs = std::filesystem;

FileScanner::FileScanner(Memory::LongTermMemory& ltm) 
    : m_ltm(ltm) {}

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

        auto options = fs::directory_options::skip_permission_denied;
        for (const auto& entry : fs::recursive_directory_iterator(root_path, options)) {
            if (m_stop_requested.load()) break;

            while (Ronin::Kernel::Intent::g_thermal_state == Ronin::Kernel::Intent::ThermalState::SEVERE) {
                if (m_stop_requested.load()) break;
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }

            if (entry.is_regular_file()) {
                const auto& path = entry.path();
                std::string abs_path = fs::absolute(path).string();
                
                if (abs_path.find("/data/") != std::string::npos) continue;

                std::string filename = path.filename().string();
                std::string extension = path.extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
                
                bool is_whitelisted = (extension == ".md" || extension == ".json" || 
                                     extension == ".yml" || extension == ".yaml" || 
                                     extension == ".zig" || extension == ".py" ||
                                     extension == ".cpp" || extension == ".h" ||
                                     extension == ".txt" || extension == ".pdf" ||
                                     extension == ".mp3" || extension == ".m4a" ||
                                     extension == ".mp4" || extension == ".mkv" ||
                                     extension == ".wav");

                if (!is_whitelisted) continue;

                auto ftime = fs::last_write_time(path);
                auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
                uint64_t modified = static_cast<uint64_t>(sctp.time_since_epoch().count());

                if (m_ltm.indexFile(filename, abs_path, extension, modified)) {
                    indexed_count++;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    } catch (...) {}

    LOGI(TAG, "Background scan completed. Indexed %d new files.", indexed_count);
    m_is_running.store(false);
}

} // namespace Ronin::Kernel::Capability
