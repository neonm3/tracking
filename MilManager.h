#pragma once

#ifdef HAVE_MIL
#include <mil.h>
#else
typedef long long MIL_ID;
typedef long long MIL_INT;
#ifndef M_NULL
#define M_NULL 0
#endif
#endif

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class MilManager
{
public:
    MilManager() = default;
    ~MilManager();

    static MilManager& instance();

    bool builtWithMil() const;
    bool hasSystem() const;
    std::string summaryLine() const;
    std::string dumpDevices(int maxDev = 64, bool verbose = false);

    void shutdown();

    std::string lastError() const;

    bool ensureDigitizer(int deviceNum, const std::string& dcfPath);
    bool grabToRGBA8(int deviceNum, std::vector<uint8_t>& outRGBA, int& outWidth, int& outHeight);
    bool grabGridToRGBA8(int cameraCount, int gridCols, int deviceOffset, const std::string& dcfPath,
                         std::vector<uint8_t>& outRGBA, int& outWidth, int& outHeight);

    MilManager(const MilManager&) = delete;
    MilManager& operator=(const MilManager&) = delete;

private:
#ifdef HAVE_MIL
    MIL_ID _appId = M_NULL;
    MIL_ID _sysId = M_NULL;
#endif

    struct Dig
    {
        bool allocated = false;
        int deviceNum = -1;
        std::string dcfPath;
        MIL_ID digId = M_NULL;
        MIL_ID grabBuf = M_NULL;
        MIL_INT sizeX = 0;
        MIL_INT sizeY = 0;
        MIL_INT bands = 0;
    };

    bool ensureSystem();
    bool allocDig(Dig& d, int deviceNum, const std::string& dcfPath);
    void freeDig(Dig& d);

    mutable std::mutex _mtx;
    std::vector<Dig> _digs;
    std::string _lastError;

private:
#ifdef HAVE_MIL
    // If you want DCF support later: implement UTF8->MIL_TEXT conversion here.
#endif
};
