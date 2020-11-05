#pragma once

#include <sstream>
#include <memory>
#include <fstream>
#include <thread>
#include <mutex>
#include <iostream>

namespace LogUtils {

    enum LogLevel {
        TRACE = 0,
        DEBUG = 1,
        INFO = 2,
        WARNING = 3,
        ERR = 4,
        FATAL = 5
    };

    constexpr auto LOGGER_TIME_FORMAT = "%Y-%m-%d %H:%M:%S";

    /*
    * Helper function to get current system timestamp defined as per LOGGER_TIME_FORMAT
    *
    */
    std::string getCurrentTimestamp();

    /*
    * Helper function to get log level string
    *
    * @param loggerLevel
    *     Log level for the logging session
    */
    std::string getLogLevelString(LogLevel logLevel);

    /*
    * Helper function to obtain current file name
    *
    * @param fileName
    *     Current logging source file with full path
    *
    * @return filename with path stripped off
    */
    std::string getCurrentLogModuleFileName(std::string fileName);

    /*
    * Thread safe utility logger class to dump messages based on log level
    *
    */
    class ALogger {
    public:

        /*
        * Constructor with logger level set as input parameter
        *
        * @param loggerLevel
        *     Log level for the logging session
        *
        * @param outputFilePath
        *     Output file path where log should be written
        */
        explicit ALogger(LogLevel loggerLevel, std::string outputFilePath);

        /*
        * Set log file to the logger
        * @param logFile
        *     Log file to be set for the current logging session
        *
        * @param loggerLevel
        *     Log level for the logging session
        */
        ALogger(std::string logFile, LogLevel loggerLevel);

        ~ALogger();

        /*
        * @name Copy and move
        *
        * No copying and moving allowed. These objects are always held by a smart pointer
        */
        ALogger(const ALogger&) = delete;
        ALogger& operator=(const ALogger&) = delete;

        ALogger(ALogger&&) = delete;
        ALogger& operator=(ALogger&&) = delete;

        /*
        * Set log file to the logger
        * @param logFile
        *     Log file to be set for the current logging session
        */
        void setLogFile(std::string logFile) {
            logFileName = logFile;
        }

        /*
        * Flush content into the log file
        * @param content
        *     Input content to be logged
        */
        void writeLog(std::string content) {
            outLogFile = std::make_unique<std::fstream>(logFileName, std::ios_base::app);
            if (outLogFile) {
                *outLogFile << content << "\n";
                outLogFile->close();
            }
        }

        /*
        * Get application's log level set for the logger
        * @return 
        *     Application log level
        */
        LogLevel getLogLevel() const {
            return logLevel;
        }

    private:

        std::string logFileName = "";
        LogLevel logLevel = LogLevel::INFO;
        std::unique_ptr<std::fstream> outLogFile;
    };

    using ALogger_p = std::unique_ptr<ALogger>;
    extern inline ALogger_p appLogger = nullptr; // Static logger object to be used by the App
    static std::mutex logMutex; // Static logger object to be used by the App

    /*
    * processLogginParameter - Two overloads are used when processing variadic string parameters  
    * from the logging macro ALOG that we used to log any numbers of paramters. 
    */
    inline std::string processLoggingParameter(std::string val) {
        return val;
    }
    template<typename... Args>
    inline std::string processLoggingParameter(std::string val, Args... args) {
        return val + std::string(" ") + processLoggingParameter(args...);
    }

    /*
    * Inline helper function to construct and write log info from a variadic parameters list into the log file
    */
    template<typename... Args>
    inline void constructAndWriteLog(LogLevel level, std::string fileName, long lineNumber, std::string funcName, Args... args) {
        std::lock_guard<std::mutex> logLock(logMutex);
        if (level >= appLogger->getLogLevel()) {
            std::string curfile = getCurrentLogModuleFileName(fileName);
            std::stringstream ss;
            ss << "[" << getCurrentTimestamp() << "]" << " " << getLogLevelString(level) << ": "
                << "THR" << "(" << std::this_thread::get_id() << ")" << " " << curfile << ":" << lineNumber << "->"
                << funcName << "  " << processLoggingParameter(args...);
            appLogger->writeLog(ss.str());
        }
    }

    /*
    * Various key value macros to be used when dumping variable values to the logger
    */
    #define NV(field) getNV((#field), field)
    #define NVV(field, value) getNV((#field), value)

    /*
    * Master logger macro that takes variadic string parameters to dump various debug contents into log file
    */
    #define ALOG(level, ...) constructAndWriteLog(level, __FILE__, __LINE__, __func__, ##__VA_ARGS__)

    /*
    * Templated helper function to stringfy key value pair for a variable. This os internally called by all NV macros
    * @param varName
    *     Variable name to be printed
    *
    * @param var
    *     Templated variable whose value need to be printed
    *
    * @return
    */
    template<typename T>
    inline std::string getNV(std::string varName, T var) {
        std::stringstream ss;
        ss << varName;
        ss << "=";
        ss << var;
        return ss.str();
    }

}; // End of namespace LogUtils