#include "capabilities/file_scanner.h"
#include "intent_engine.h"
#include "ronin_log.h"
#include <chrono>

#define TAG "RoninFileScanner"

namespace Ronin::Kernel::Capability {

namespace fs = std::filesystem;

FileScanner::FileScanner(Memory::LongTermMemory& ltm) : m_ltm(ltm) {}

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
    LOGI(TAG, "Background scan started: %s", root_path.c_str());

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
                std::string abs_path = path.absolute().string();
                std::string extension = path.extension().string();
                
                auto ftime = fs::last_write_time(path);
                auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
                uint64_t modified = static_cast<uint64_t>(sctp.time_since_epoch().count());

                // Index into L3 Deep-store
                m_ltm.indexFile(filename, abs_path, extension, modified);
            }

            // Yield CPU to maintain low-priority background operation
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    } catch (const std::exception& e) {
        LOGE(TAG, "Exception during file scan: %s", e.what());
    }

    LOGI(TAG, "Background scan completed.");
    m_is_running.store(false);
}

} // namespace Ronin::Kernel::Capability
