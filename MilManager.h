#pragma once

#include <string>
#include <vector>
#include <mutex>

#include <cstdint>

#if defined(HAVE_MIL)
#include <mil.h>
#endif

class MilManager
{

public:
    static MilManager& instance();



    bool builtWithMil() const;
    std::string summaryLine() const;
    std::string dumpDevices() const;

    std::string diagnostics_NoLock() const;

    // optional but useful for UI logs
    const std::string& lastError() const;

    // Ensure digitizer allocated (camIdx: 0 => M_DEV0, 1 => M_DEV1, etc.)
    bool ensureDigitizer(int camIdx);

    // Convenience overloads (use whichever your BasicFilterTOP uses)
    bool grabToRGBA8(int camIdx, int width, int height, std::vector<uint8_t>& outRGBA);
    bool grabToRGBA8(int camIdx, int width, int height, uint8_t* outRGBA, size_t outBytes);

    bool grabGridToRGBA8(int gridCols, int gridRows, int tileW, int tileH, std::vector<uint8_t>& outRGBA);
    bool grabGridToRGBA8(int gridCols, int gridRows, int tileW, int tileH, uint8_t* outRGBA, size_t outBytes);


    // --- Compatibility shims -------------------------------------------------
 // Some older call sites pass extra "unused" parameters (e.g. logging flags,
 // stride, etc.). These templated overloads intentionally ignore trailing args
 // and forward to the canonical implementations above.

    template<typename... Extra>
    std::string dumpDevices(Extra&&...) const { return dumpDevices(); }

    template<typename... Extra>
    bool ensureDigitizer(int camIdx, Extra&&...) { return ensureDigitizer(camIdx); }

    // grabToRGBA8 compatibility shims
    template<typename... Extra>
    bool grabToRGBA8(int camIdx, int width, int height,
        std::vector<uint8_t>& outRGBA, Extra&&...)
    {
        return grabToRGBA8(camIdx, width, height, outRGBA);
    }

    template<typename Ptr, typename Size, typename... Extra,
        typename = std::enable_if_t<std::is_pointer_v<std::decay_t<Ptr>> && !std::is_same_v<std::decay_t<Ptr>, std::vector<uint8_t>*>>>
    bool grabToRGBA8(int camIdx, int width, int height,
        Ptr outRGBA, Size outBytes, Extra&&...)
    {
        using Pointee = std::remove_pointer_t<std::decay_t<Ptr>>;
        using BytePtr = std::conditional_t<std::is_const_v<Pointee>, const uint8_t*, uint8_t*>;

        return grabToRGBA8(camIdx, width, height,
            reinterpret_cast<BytePtr>(outRGBA),
            static_cast<size_t>(outBytes));
    }

    // grabGridToRGBA8 compatibility shims
    template<typename... Extra>
    bool grabGridToRGBA8(int gridCols, int gridRows, int tileW, int tileH,
        std::vector<uint8_t>& outRGBA, Extra&&...)
    {
        return grabGridToRGBA8(gridCols, gridRows, tileW, tileH, outRGBA);
    }

    template<typename Ptr, typename Size, typename... Extra,
        typename = std::enable_if_t<std::is_pointer_v<std::decay_t<Ptr>> && !std::is_same_v<std::decay_t<Ptr>, std::vector<uint8_t>*>>>
    bool grabGridToRGBA8(int gridCols, int gridRows, int tileW, int tileH,
        Ptr outRGBA, Size outBytes, Extra&&...)
    {
        using Pointee = std::remove_pointer_t<std::decay_t<Ptr>>;
        using BytePtr = std::conditional_t<std::is_const_v<Pointee>, const uint8_t*, uint8_t*>;

        return grabGridToRGBA8(gridCols, gridRows, tileW, tileH,
            reinterpret_cast<BytePtr>(outRGBA),
            static_cast<size_t>(outBytes));
    }

private:
    MilManager();
    ~MilManager();

    MilManager(const MilManager&) = delete;
    MilManager& operator=(const MilManager&) = delete;


    // System selection cache
    MIL_STRING _cachedSysDesc;
    MIL_INT    _cachedSysDevNum = 0;

    // MIL UI spam guard
    bool _milErrorPrintDisabled = false;
    bool _sysAllocAttempted = false;

    // Digitizer discovery
    std::vector<MIL_INT> _validDigDevs;
    std::string _lastProbeReport;

    void probeDigitizerAllocStyles_NoLock();

private:
#if defined(HAVE_MIL)
    struct Dig
    {
        MIL_ID dig = M_NULL;
        MIL_ID grabBuf = M_NULL;   // 8-bit mono buffer (simple + robust)
        MIL_INT w = 0;
        MIL_INT h = 0;
    };
#endif

    bool ensureSystem();
    bool discoverDigitizers_NoLock();
#if defined(HAVE_MIL)
    bool allocDig(int camIdx);
    void freeDig(int camIdx);
#endif

private:
    mutable std::recursive_mutex _mtx;
    std::string _lastError;

#if defined(HAVE_MIL)
    MIL_ID _appId = M_NULL;
    MIL_ID _sysId = M_NULL;
    std::vector<Dig> _digs;
#endif
};
