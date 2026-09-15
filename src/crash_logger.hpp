#ifndef CRASH_LOGGER_HPP
#define CRASH_LOGGER_HPP

#include <fstream>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>

class CrashLogger {
public:
    static void SaveCrashFrame(const void* frame, size_t size, const std::string& reason) {
        mkdir("crashes", 0755);
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        std::ostringstream filename;
        filename << "crashes/crash_" << reason << "_" << now << ".bin";
        
        std::ofstream outfile(filename.str(), std::ios::binary);
        outfile.write(reinterpret_cast<const char*>(frame), size);
    }
};

#endif
