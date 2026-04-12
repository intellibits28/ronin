#pragma once

#include <string>
#include <vector>
#include <sqlite3.h>
#include <mutex>

namespace Ronin::Kernel::Capability {

class FileSearchEngine {
public:
    FileSearchEngine(const std::string& db_path);
    ~FileSearchEngine();

    // Index a file name into FTS5
    bool indexFile(const std::string& filename, const std::string& path);

    // Search for files matching a query
    std::vector<std::string> searchFiles(const std::string& query);

private:
    sqlite3* m_db = nullptr;
    std::mutex m_mutex;

    bool initSchema();
};

} // namespace Ronin::Kernel::Capability
