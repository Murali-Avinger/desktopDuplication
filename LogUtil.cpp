
#include "LogUtil.hpp"
#include "Version.h"

#include <windows.h>
#include <psapi.h>
#include <chrono>
#include <ctime>

namespace LogUtils {

    ALogger::ALogger(LogLevel loggerLevel, std::string outputFilePath) : logLevel(std::move(loggerLevel)) {
        char szPath[MAX_PATH];
        GetModuleBaseNameA(GetCurrentProcess(), GetModuleHandleA(NULL), szPath, MAX_PATH);
        std::string processName = szPath;
        auto pos = processName.rfind('.');
        if (pos != std::string::npos) {
            logFileName = outputFilePath + "\\" + processName.substr(0, pos) + ".log";
            outLogFile = std::make_unique<std::fstream>(logFileName, std::ios_base::app);
            *outLogFile << "VideoCaptureVersion=" << CAPTURE_VERSION << "\n";
        }
    }

    ALogger::ALogger(std::string logFile, LogLevel loggerLevel) : logFileName(std::move(logFile)), logLevel(std::move(loggerLevel)) {
        outLogFile = std::make_unique<std::fstream>(logFileName, std::ios_base::app);
        *outLogFile << "VideoCaptureVersion=" << CAPTURE_VERSION << "\n";
    }

    ALogger::~ALogger() {
        if (outLogFile) {
            outLogFile->close();
        }
    }

    std::string getCurrentTimestamp() {
        auto timePoint = std::chrono::system_clock::now();
        std::time_t currentTime = std::chrono::system_clock::to_time_t(timePoint);
        struct tm newTimeInfo;
        localtime_s(&newTimeInfo, &currentTime);

        char timeBuffer[128];
        size_t string_size = strftime(timeBuffer, sizeof(timeBuffer), LogUtils::LOGGER_TIME_FORMAT, &newTimeInfo);

        auto duration = timePoint.time_since_epoch();
        int milliseconds = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count()) % 1000;
        std::string result = timeBuffer;
        result += ".";
        result += std::to_string(milliseconds);

        return result;
    }

    std::string getCurrentLogModuleFileName(std::string fileName) {
        std::string curfile = fileName;
        auto pos = curfile.rfind('\\');
        if (pos != std::string::npos) {
            curfile = curfile.substr(pos + 1);
        }
        return curfile;
    }

    std::string getLogLevelString(LogLevel logLevel) {
        std::string levelStr = "";
        switch (logLevel) {

        case LogLevel::TRACE:
            levelStr = "TRACE";
            break;
        case LogLevel::DEBUG:
            levelStr = "DEBUG";
            break;
        case LogLevel::INFO:
            levelStr = "INFO";
            break;
        case LogLevel::WARNING:
            levelStr = "WARNING";
            break;
        case LogLevel::ERR:
            levelStr = "ERROR";
            break;
        case LogLevel::FATAL:
            levelStr = "FATAL";
            break;
        default:
            break;
        }
        return levelStr;
    }

} // End of namespace 