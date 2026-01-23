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
#include <string>
#include <vector>

#if defined(_MSC_VER)
  #if _MSC_VER >= 1700
    #define MILMANAGER_HAS_STD_MUTEX 1
  #endif
#else
  #if __cplusplus >= 201103L
    #define MILMANAGER_HAS_STD_MUTEX 1
  #endif
#endif

#if MILMANAGER_HAS_STD_MUTEX
  #include <mutex>
#endif

namespace mil_detail
{
#if MILMANAGER_HAS_STD_MUTEX
    typedef std::mutex Mutex;
    template <typename T>
    class LockGuard
    {
    public:
        explicit LockGuard(T& mutex) : _mutex(mutex) { _mutex.lock(); }
        ~LockGuard() { _mutex.unlock(); }
    private:
        T& _mutex;
        LockGuard(const LockGuard&);
        LockGuard& operator=(const LockGuard&);
    };
#else
    class Mutex
    {
    public:
        void lock() {}
        void unlock() {}
    };

    template <typename T>
    class LockGuard
    {
    public:
        explicit LockGuard(T& mutex) : _mutex(mutex) { _mutex.lock(); }
        ~LockGuard() { _mutex.unlock(); }
    private:
        T& _mutex;
        LockGuard(const LockGuard&);
        LockGuard& operator=(const LockGuard&);
    };
#endif
}

class MilManager
{
public:
    MilManager();
    ~MilManager();

    static MilManager& instance();

    bool builtWithMil() const;
    bool hasSystem() const;
    // Returns a one-line status summary for MIL allocation state.
    std::string summaryLine() const;
    // Probes digitizers via MdigAlloc. maxDev is clamped to 1..256 in the implementation.
    std::string dumpDevices(int maxDev = 64, bool verbose = false);

    void shutdown();

    std::string lastError() const;

    // Ensures a digitizer is allocated for deviceNum (reallocates if dcfPath changes).
    bool ensureDigitizer(int deviceNum, const std::string& dcfPath);
    bool grabToRGBA8(int deviceNum, std::vector<uint8_t>& outRGBA, int& outWidth, int& outHeight);
    // Grabs multiple cameras into a tiled RGBA buffer. cameraCount is clamped to 1..24.
    bool grabGridToRGBA8(int cameraCount, int gridCols, int deviceOffset, const std::string& dcfPath,
                         std::vector<uint8_t>& outRGBA, int& outWidth, int& outHeight);

private:
    typedef mil_detail::Mutex Mutex;
    typedef mil_detail::LockGuard<Mutex> LockGuard;

#ifdef HAVE_MIL
    MIL_ID _appId;
    MIL_ID _sysId;
#endif

    struct Dig
    {
        Dig();

        bool allocated;
        int deviceNum;
        std::string dcfPath;
        MIL_ID digId;
        MIL_ID grabBuf;
        MIL_INT sizeX;
        MIL_INT sizeY;
        MIL_INT bands;
    };

    bool ensureSystem();
    bool allocDig(Dig& d, int deviceNum, const std::string& dcfPath);
    void freeDig(Dig& d);

    mutable Mutex _mtx;
    std::vector<Dig> _digs;
    std::string _lastError;

private:
    MilManager(const MilManager&);
    MilManager& operator=(const MilManager&);

#ifdef HAVE_MIL
    // If you want DCF support later: implement UTF8->MIL_TEXT conversion here.
#endif
};
