#pragma once
#include <Windows.h>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <set>
#include <string>

class Logger
{
public:
    static void Initialize(HMODULE module)
    {
        char modulePath[MAX_PATH]{};
        if (GetModuleFileNameA(module, modulePath, MAX_PATH)) {
            std::string path(modulePath);
            const auto slash = path.find_last_of("\\/");
            if (slash != std::string::npos)
                Path() = path.substr(0, slash + 1) + "portal2vr.log";
        }
        const bool rotated = RotateIfLarge(Path());
        Write("Portal 2 VR Enriched build " __DATE__ " " __TIME__ "; process architecture: x86");
        if (rotated)
            Write("Earlier sessions moved to " + FileName(RotatedPath(Path())));
    }

    static void Write(const std::string &message)
    {
        std::lock_guard<std::mutex> lock(Mutex());
        std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_s(&local, &now);
        std::ofstream stream(Path(), std::ios::app);
        if (stream)
            stream << std::put_time(&local, "%Y-%m-%d %H:%M:%S") << " " << message << '\n';
        OutputDebugStringA(("Portal 2 VR Enriched: " + message + "\n").c_str());
    }

    // A file next to portal2vr.log (the d3d9.dll directory).
    static std::string SiblingPath(const char *fileName)
    {
        std::lock_guard<std::mutex> lock(Mutex());
        const std::string &path = Path();
        const auto slash = path.find_last_of("\\/");
        return (slash == std::string::npos ? std::string() : path.substr(0, slash + 1)) + fileName;
    }

    // Same timestamped line format as Write, in a sibling file and without
    // the debugger echo; used for high-volume logs such as the perf log.
    static void WriteSibling(const char *fileName, const std::string &message)
    {
        const std::string path = SiblingPath(fileName);
        std::lock_guard<std::mutex> lock(Mutex());
        if (RotationChecked().insert(path).second)
            RotateIfLarge(path);
        std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_s(&local, &now);
        std::ofstream stream(path, std::ios::app);
        if (stream)
            stream << std::put_time(&local, "%Y-%m-%d %H:%M:%S") << " " << message << '\n';
    }

    // Logs only ever append. At a file's first write of a session, one that has
    // passed the limit becomes "<name>.old.log" and replaces the one before it,
    // so each log keeps the current and the previous generation.
    static constexpr unsigned long long kRotateBytes = 4ull << 20;

    static bool ShouldRotate(unsigned long long sizeBytes) { return sizeBytes >= kRotateBytes; }

    static std::string RotatedPath(const std::string &path)
    {
        const auto slash = path.find_last_of("\\/");
        const auto dot = path.find_last_of('.');
        if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
            return path + ".old";
        return path.substr(0, dot) + ".old" + path.substr(dot);
    }

private:
    static std::string FileName(const std::string &path)
    {
        const auto slash = path.find_last_of("\\/");
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    static bool RotateIfLarge(const std::string &path)
    {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data))
            return false;
        const unsigned long long size =
            (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        return ShouldRotate(size) &&
            MoveFileExA(path.c_str(), RotatedPath(path).c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
    }

    static std::set<std::string> &RotationChecked()
    {
        static std::set<std::string> checked;
        return checked;
    }

    static std::string &Path()
    {
        static std::string path = "portal2vr.log";
        return path;
    }
    static std::mutex &Mutex()
    {
        static std::mutex mutex;
        return mutex;
    }
};
