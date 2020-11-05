#pragma once
#include <sys/types.h>
#include <sys/stat.h>

namespace FileUtils {

    std::time_t getLastWriteTime(std::string fileName) {
        std::time_t modifiedTime = 0;
        struct _stat buf;
        int result = _stat(fileName.c_str(), &buf);
        if (result == 0) {
            modifiedTime = buf.st_mtime;
        }

        return modifiedTime;
    }
}