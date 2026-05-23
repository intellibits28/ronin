#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <sqlite3.h>

namespace Ronin::Kernel::Data {

enum class MemoryState : int {
    ACTIVE = 0,
    COLD = 1,
    ARCHIVED = 2,
    FORGOTTEN = 3,
    TOMBSTONED = 4
};

struct MemoryEntry {
    int id;
    std::string raw_text_mm;
    std::string segmented_text_mm;
    MemoryState state;
    int64_t timestamp;
    std::string source;
};

class MemoryDatabase {
public:
    explicit MemoryDatabase(const std::string& db_path);
    ~MemoryDatabase();

    // Prevent copying
    MemoryDatabase(const MemoryDatabase&) = delete;
    MemoryDatabase& operator=(const MemoryDatabase&) = delete;

    bool isOpen() const { return m_db != nullptr; }

    // Core Data Operations
    bool insertMemory(const std::string& raw_text, const std::string& segmented_text, MemoryState state = MemoryState::ACTIVE, const std::string& source = "user");
    bool updateMemoryState(int id, MemoryState new_state);
    bool deleteMemory(int id);
    
    // Search Operations
    std::vector<MemoryEntry> searchFTS(const std::string& query, int limit = 3);
    
    // Lifecycle Management
    int purgeTombstoned();

private:
    sqlite3* m_db = nullptr;
    mutable std::mutex m_mutex;

    bool initSchema();
    bool executeSql(const std::string& sql);
};

} // namespace Ronin::Kernel::Data
