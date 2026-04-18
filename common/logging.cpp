#include "common/logging.hpp"

#include <chrono>
#include <filesystem>

namespace ll {

std::uint64_t wall_time_ms() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

CsvLog::~CsvLog() { close(); }

bool CsvLog::open(const std::string& path, const std::string& header_line) {
    std::lock_guard<std::mutex> g(mu_);
    if (fp_) return true;

    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }

    fp_ = std::fopen(path.c_str(), "w");
    if (!fp_) return false;
    std::fputs(header_line.c_str(), fp_);
    std::fputc('\n', fp_);
    return true;
}

void CsvLog::write_line(const std::string& line) {
    std::lock_guard<std::mutex> g(mu_);
    if (!fp_) return;
    std::fputs(line.c_str(), fp_);
    std::fputc('\n', fp_);
}

void CsvLog::flush() {
    std::lock_guard<std::mutex> g(mu_);
    if (fp_) std::fflush(fp_);
}

void CsvLog::close() {
    std::lock_guard<std::mutex> g(mu_);
    if (fp_) {
        std::fclose(fp_);
        fp_ = nullptr;
    }
}

} // namespace ll
