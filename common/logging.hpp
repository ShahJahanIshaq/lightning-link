#pragma once

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

namespace ll {

// Tiny thread-safe CSV sink. Row format is caller-controlled; this class just
// handles file open/header/append/flush.
class CsvLog {
public:
    CsvLog() = default;
    ~CsvLog();

    // Open file and (if fresh) write a header line. Safe to call once.
    bool open(const std::string& path, const std::string& header_line);

    // Append a pre-formatted CSV line (no trailing newline required).
    void write_line(const std::string& line);

    void flush();
    void close();

    bool is_open() const { return fp_ != nullptr; }

private:
    std::FILE*  fp_ = nullptr;
    std::mutex  mu_;
};

std::uint64_t wall_time_ms();

} // namespace ll
