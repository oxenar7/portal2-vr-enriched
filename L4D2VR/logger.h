#pragma once
#include <Windows.h>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
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
        Write("Portal 2 VR Enriched build " __DATE__ " " __TIME__ "; process architecture: x86");
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
        std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_s(&local, &now);
        std::ofstream stream(path, std::ios::app);
        if (stream)
            stream << std::put_time(&local, "%Y-%m-%d %H:%M:%S") << " " << message << '\n';
    }

private:
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
