#include "MilManager.h"
#include <type_traits>
#include <string>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <Windows.h>

static inline void setErr(MilManager& mm, std::string& dst, const std::string& msg)
{
    dst = msg;

#if defined(HAVE_MIL)
    if (!dst.empty())
    {
        static thread_local bool inDiag = false;
        if (!inDiag)
        {
            inDiag = true;
            dst += "\n\n";
            dst += mm.diagnostics_NoLock();   // <-- automatic dump
            inDiag = false;
        }
    }
#endif
}



MilManager& MilManager::instance()
{
    static MilManager g;
    return g;
}

MilManager::MilManager()
{
#if !defined(HAVE_MIL)
    setErr(_lastError, "MIL not compiled in (define HAVE_MIL).");
#else
    // Lazy-init in ensureSystem()
#endif
}

MilManager::~MilManager() 
{
#if defined(HAVE_MIL)
    std::lock_guard<std::recursive_mutex> lk(_mtx);


    for (int i = 0; i < (int)_digs.size(); ++i)
        freeDig(i);

    if (_sysId != M_NULL) { MsysFree(_sysId); _sysId = M_NULL; }
    if (_appId != M_NULL) { MappFree(_appId); _appId = M_NULL; }
#endif
}

const std::string& MilManager::lastError() const
{
    std::lock_guard<std::recursive_mutex> lk(_mtx);

    return _lastError;
}

bool MilManager::builtWithMil() const
{
#if defined(HAVE_MIL)
    return true;
#else
    return false;
#endif
}

#include <mutex>
#include <sstream>



bool MilManager::ensureSystem()
{
#if !defined(HAVE_MIL)
    setErr(_lastError, "MIL not compiled (HAVE_MIL not defined).");
    return false;
#else
    std::lock_guard<std::recursive_mutex> lk(_mtx);

    // Already good
    if (_appId != M_NULL && _sysId != M_NULL)
        return true;

    // If we've already tried and failed in this run, don't spam MsysAlloc (and MIL popups).
    if (_sysAllocAttempted && _sysId == M_NULL)
        return false;

    // 1) Allocate MIL application once
    if (_appId == M_NULL)
    {
        MappAlloc(M_DEFAULT, &_appId);
        if (_appId == M_NULL)
        {
            setErr(*this, _lastError, "MappAlloc failed. Check MIL install/runtime.");
            _sysAllocAttempted = true;
            return false;
        }
    }

    // 2) Disable MIL interactive error printing once (prevents popup dialogs in TouchDesigner)
    if (!_milErrorPrintDisabled)
    {
        MappControl(M_DEFAULT, M_ERROR, M_PRINT_DISABLE);
        _milErrorPrintDisabled = true;
    }

    auto scoreDescriptor = [](const MIL_STRING& d) -> int
        {
            // Prefer framegrabber-backed systems first.
            // Generic host GigE Vision should be last.
            if (d.find(MIL_TEXT("CONCORD")) != MIL_STRING::npos) return 300;
            if (d.find(MIL_TEXT("GEVIQ")) != MIL_STRING::npos) return 250;
            if (d.find(MIL_TEXT("GENTL")) != MIL_STRING::npos) return 100;
            if (d.find(MIL_TEXT("GIGE")) != MIL_STRING::npos) return 50;
            return 0;
        };


    auto tryAllocSystem = [&](const MIL_STRING& desc, MIL_INT sysDevNum, MIL_ID& outSys) -> bool
        {
            outSys = M_NULL;
            MsysAlloc(desc.c_str(), M_DEV0 + sysDevNum, M_DEFAULT, &outSys);
            return outSys != M_NULL;
        };

    auto onSystemAllocated = [&](const MIL_STRING& desc, MIL_INT sysDevNum, MIL_ID sys) -> bool
        {
            // Commit system
            _sysId = sys;

            // Cache selection
            _cachedSysDesc = desc;
            _cachedSysDevNum = sysDevNum;

            // This is a successful allocation attempt, so allow future retries if something changes
            _sysAllocAttempted = false;

            // New system => previous digitizer mapping may be stale
            _validDigDevs.clear();

            // Try to discover cameras/digitizers, but don't fail system allocation if none found yet.
            discoverDigitizers_NoLock();

            if (_validDigDevs.empty())
            {
                setErr(*this, _lastError,
                    "Concord system allocated, but no digitizers detected yet. See diagnostics below.");
            }
            else
            {
                setErr(*this, _lastError, "");
            }
            return true;


        };

    // 3) Try cached system first
    if (!_cachedSysDesc.empty())
    {
        MIL_ID sys = M_NULL;
        if (tryAllocSystem(_cachedSysDesc, _cachedSysDevNum, sys))
        {
            return onSystemAllocated(_cachedSysDesc, _cachedSysDevNum, sys);
        }
        // Cached failed; fall through to scan.
    }

    // 4) Enumerate installed systems and try Concord only
    MIL_INT sysCount = 0;
    MappInquire(M_DEFAULT, M_INSTALLED_SYSTEM_COUNT, &sysCount);

    if (sysCount <= 0)
    {
        setErr(*this, _lastError, "No MIL systems reported (M_INSTALLED_SYSTEM_COUNT=0). Check MIL drivers/licensing.");
        _sysAllocAttempted = true;
        return false;
    }

    const MIL_INT kMaxSystemDevToTry = 8;

    for (MIL_INT i = 0; i < sysCount; ++i)
    {
        MIL_STRING desc;
        MappInquire(M_DEFAULT, M_INSTALLED_SYSTEM_DESCRIPTOR + i, desc);

        // Skip host and gentl
        if (desc == MIL_TEXT("M_SYSTEM_HOST") || desc == MIL_TEXT("M_SYSTEM_GENTL"))
            continue;

        // Only consider Concord systems
        if (!scoreDescriptor(desc))
            continue;

        for (MIL_INT sysDevNum = 0; sysDevNum < kMaxSystemDevToTry; ++sysDevNum)
        {
            MIL_ID sys = M_NULL;
            if (tryAllocSystem(desc, sysDevNum, sys))
            {
                return onSystemAllocated(desc, sysDevNum, sys);
            }
        }
    }

    // Mark that we've tried, so we don't repeatedly pop dialogs on every cook.
    _sysAllocAttempted = true;

    setErr(*this, _lastError,
        "Failed to allocate a Concord MIL system. "
        "If MultiCameraDisplay works but this fails, log installed system descriptors and ensure the CONCORD descriptor is selected. "
        "Otherwise check Matrox Concord driver/service, MILConfig registration, and MIL x64 runtime.");

    return false;
#endif
}

void MilManager::probeDigitizerAllocStyles_NoLock()
{
#if !defined(HAVE_MIL)
    return;
#else
    std::ostringstream os;
    os << "Probe digitizer alloc styles:\n";

    if (_sysId == M_NULL)
    {
        os << "  sys is null\n";
        _lastProbeReport = os.str();
        return;
    }

    auto tryAlloc = [&](const char* label, MIL_INT devNum, const MIL_TEXT_CHAR* dcf)
        {
            MIL_ID d = M_NULL;
            MdigAlloc(_sysId, devNum, dcf, M_DEFAULT, &d);
            os << "  " << label << ": " << (d ? "OK" : "FAIL") << "\n";
            if (d) MdigFree(d);
        };

    // 1) GigE via special devnum (what you tried)
    tryAlloc("dev=M_GIGE_VISION, dcf=M_DEFAULT", M_GIGE_VISION, MIL_TEXT("M_DEFAULT"));

    // 2) GigE via default devnum but GigE DCF string (common pattern in some setups)
    tryAlloc("dev=M_DEFAULT, dcf=M_GIGE_VISION", M_DEFAULT, MIL_TEXT("M_GIGE_VISION"));

    // 3) Plain default
    tryAlloc("dev=M_DEFAULT, dcf=M_DEFAULT", M_DEFAULT, MIL_TEXT("M_DEFAULT"));

    // 4) Oldschool dev0
    tryAlloc("dev=M_DEV0, dcf=M_DEFAULT", M_DEV0, MIL_TEXT("M_DEFAULT"));

    _lastProbeReport = os.str();
#endif
}


bool MilManager::discoverDigitizers_NoLock()
{
#if !defined(HAVE_MIL)
    return false;
#else
    _validDigDevs.clear();
    _lastProbeReport.clear();

    if (_sysId == M_NULL)
    {
        _lastProbeReport = "discoverDigitizers: sys is null.";
        return false;
    }

    // Concord PoE often exposes a small number of digitizer indices; probe a bit wider.
    const int kMaxDigToProbe = 16;

    std::ostringstream os;
    os << "Digitizer probe (M_DEV0.." << (kMaxDigToProbe - 1) << ")\n";

    for (int i = 0; i < kMaxDigToProbe; ++i)
    {
        const MIL_INT dev = M_DEV0 + i;
        MIL_ID dig = M_NULL;

        // IMPORTANT: use "M_DEFAULT" string (matches Matrox examples)
        MdigAlloc(_sysId, dev, MIL_TEXT("M_DEFAULT"), M_DEFAULT, &dig);

        if (dig == M_NULL)
        {
            os << "  DEV" << i << ": alloc FAIL\n";
            continue;
        }

        // Allocation succeeded => this is a real digitizer endpoint on this system.
        // Now optionally check presence.
        MIL_INT present = MdigInquire(dig, M_CAMERA_PRESENT, M_NULL);

        // Some setups return M_ERROR / junk for presence; interpret conservatively:
        // - if it explicitly says NO, skip
        // - otherwise keep it
       // if (present == M_NO)
       // {
        //    os << "  DEV" << i << ": alloc OK, camera_present=NO (skipping)\n";
        //    MdigFree(dig);
        //    continue;
        //}

        _validDigDevs.push_back(dev);
        os << "  DEV" << i << ": alloc OK, camera_present="
            << ((present == M_YES) ? "YES" : "UNKNOWN") << " (keeping)\n";

        MdigFree(dig);
    }

    _lastProbeReport = os.str();

    return !_validDigDevs.empty();
#endif
}

/**
static std::string milStringToStd(const MIL_STRING& s)
{
//#if defined(MIL_TEXT_CHAR) && (sizeof(MIL_TEXT_CHAR) == sizeof(wchar_t))
    // MIL built in Unicode (Windows default)
    if (s.empty()) return std::string();
    std::wstring ws(s.c_str());
    return std::string(ws.begin(), ws.end()); // basic UTF-16 → ASCII fallback
#else
    // MIL built in ANSI
    return std::string(s.c_str());
#endif
}*/


std::string MilManager::summaryLine() const
{
#if !defined(HAVE_MIL)
    return "MIL: compiled=no";
#else
    std::lock_guard<std::recursive_mutex> lk(_mtx);
    std::ostringstream oss;
    oss << "MIL: compiled=yes, app=" << (_appId != M_NULL ? "ok" : "null")
        << ", sys=" << (_sysId != M_NULL ? "ok" : "null")
        << ", digs=" << _digs.size();
    if (!_lastError.empty())
        oss << " (err: " << _lastError << ")";
    return oss.str();
#endif
}

std::string MilManager::dumpDevices() const
{
#if !defined(HAVE_MIL)
    return "MIL not compiled in (define HAVE_MIL).";
#else
    std::lock_guard<std::recursive_mutex> lk(_mtx);

    if (_appId == M_NULL)
        return "MIL app not allocated (MappAlloc not done).";
    if (_sysId == M_NULL)
        return "MIL system not allocated (ensureSystem() not successful).";

    std::ostringstream oss;

    // Show discovered mapping first (if any)
    oss << "Detected camera digitizers (camIdx -> M_DEVx): ";
    if (_validDigDevs.empty())
    {
        oss << "(none)\n";
    }
    else
    {
        oss << "\n";
        for (int camIdx = 0; camIdx < (int)_validDigDevs.size(); ++camIdx)
        {
            int devIdx = (int)(_validDigDevs[camIdx] - M_DEV0);
            oss << "  camIdx " << camIdx << " -> M_DEV" << devIdx << "\n";
        }
    }

    oss << "\nProbing M_DEV0..M_DEV15\n";

    for (int i = 0; i < 16; ++i)
    {
        MIL_ID d = M_NULL;
        const MIL_INT dev = M_DEV0 + i;

        MdigAlloc(_sysId, dev, MIL_TEXT("M_DEFAULT"), M_DEFAULT, &d);

        if (d == M_NULL)
        {
            oss << "  [M_DEV" << i << "] alloc FAIL\n";
            continue;
        }

        MIL_INT present = MdigInquire(d, M_CAMERA_PRESENT, M_NULL);
        oss << "  [M_DEV" << i << "] alloc OK, camera_present="
            << ((present == M_YES) ? "YES" : (present == M_NO) ? "NO" : "UNKNOWN")
            << "\n";

        MdigFree(d);
    }

    return oss.str();
#endif
}


#if defined(HAVE_MIL)

bool MilManager::allocDig(int camIdx)
{
#if !defined(HAVE_MIL)
    return false;
#else
    if (!ensureSystem())
        return false;

    std::lock_guard<std::recursive_mutex> lk(_mtx);

    if ((int)_digs.size() <= camIdx)
        _digs.resize(camIdx + 1);

    if (_digs[camIdx].dig != M_NULL)
        return true;

    MIL_ID dig = M_NULL;

    // Make sure digitizers were discovered
    if (_validDigDevs.empty() || camIdx >= (int)_validDigDevs.size())
    {
        discoverDigitizers_NoLock();
    }

    // Pick correct DEV index
    const MIL_INT dev =
        (camIdx >= 0 && camIdx < (int)_validDigDevs.size())
        ? _validDigDevs[camIdx]
        : (M_DEV0 + camIdx);

    MdigAlloc(_sysId, dev, MIL_TEXT("M_DEFAULT"), M_DEFAULT, &dig);

    if (dig == M_NULL)
    {
        std::ostringstream em;
        em << "MdigAlloc(M_DEV" << (dev - M_DEV0)
            << ") failed on current MIL system.";
        setErr(*this, _lastError, em.str());
        return false;
    }


    if (dig == M_NULL)
    {
        setErr(*this, _lastError, "MdigAlloc(M_GIGE_VISION) failed. No GigE cameras visible.");
        return false;
    }

    _digs[camIdx].dig = dig;
    _digs[camIdx].grabBuf = M_NULL;
    _digs[camIdx].w = 0;
    _digs[camIdx].h = 0;

    setErr(*this,_lastError, "");
    return true;
#endif
}

/*
bool MilManager::allocDig(int camIdx)
{
#if !defined(HAVE_MIL)
    return false;
#else
    if (camIdx < 0)
        return false;

    // ensureSystem() holds the lock and also populates _validDigDevs via discoverDigitizers_NoLock()
    if (!ensureSystem())
        return false;

    // From here on we assume _mtx is recursive; still safe to lock here for consistency.
    std::lock_guard<std::recursive_mutex> lk(_mtx);

    if (_validDigDevs.empty())
    {
        setErr(*this, _lastError, "No detected digitizers. Use 'Dump MIL Devices' and check cameras/PoE.");
        return false;
    }

    if (camIdx >= (int)_validDigDevs.size())
    {
        std::ostringstream os;
        os << "Camera index out of range. camIdx=" << camIdx
            << ", detected=" << (int)_validDigDevs.size()
            << ". Use 'Dump MIL Devices' to see valid indices.";
        setErr(*this, _lastError, os.str());
        return false;
    }

    if ((int)_digs.size() <= camIdx)
        _digs.resize((size_t)camIdx + 1);

    if (_digs[camIdx].dig != M_NULL)
        return true;

    const MIL_INT dev = _validDigDevs[camIdx];

    MIL_ID dig = M_NULL;
    MdigAlloc(_sysId, dev, MIL_TEXT("M_DEFAULT"), M_DEFAULT, &dig);

    if (dig == M_NULL)
    {
        std::ostringstream os;
        os << "MdigAlloc failed. camIdx=" << camIdx
            << ", dev=" << (int)(dev - M_DEV0) << " (M_DEV" << (int)(dev - M_DEV0) << ").";
        setErr(*this, _lastError, os.str());
        return false;
    }

    // Optional: camera-present sanity check
    MIL_INT present = MdigInquire(dig, M_CAMERA_PRESENT, M_NULL);
    if (present == M_NO)
    {
        MdigFree(dig);
        std::ostringstream os;
        os << "Digitizer allocated but camera not present. camIdx=" << camIdx
            << ", dev=M_DEV" << (int)(dev - M_DEV0) << ".";
        setErr(*this, _lastError, os.str());
        return false;
    }

    _digs[camIdx].dig = dig;
    _digs[camIdx].grabBuf = M_NULL;
    _digs[camIdx].w = 0;
    _digs[camIdx].h = 0;

    setErr(*this, _lastError, "");
    return true;
#endif
}

*/
void MilManager::freeDig(int camIdx)
{
    if (camIdx < 0 || camIdx >= (int)_digs.size()) return;

    auto& d = _digs[camIdx];
    if (d.grabBuf != M_NULL) { MbufFree(d.grabBuf); d.grabBuf = M_NULL; }
    if (d.dig != M_NULL) { MdigFree(d.dig);     d.dig = M_NULL; }
    d.w = d.h = 0;
}
#endif

bool MilManager::ensureDigitizer(int camIdx)
{
#if !defined(HAVE_MIL)
    (void)camIdx;
    return false;
#else
    std::lock_guard<std::recursive_mutex> lk(_mtx);
    return allocDig(camIdx);
#endif
}

static inline void grayToRGBA(const uint8_t* gray, int w, int h, uint8_t* rgba)
{
    const size_t n = (size_t)w * (size_t)h;
    for (size_t i = 0; i < n; ++i)
    {
        uint8_t g = gray[i];
        rgba[i * 4 + 0] = g;
        rgba[i * 4 + 1] = g;
        rgba[i * 4 + 2] = g;
        rgba[i * 4 + 3] = 255;
    }
}

bool MilManager::grabToRGBA8(int camIdx, int width, int height, std::vector<uint8_t>& outRGBA)
{
    if (width <= 0 || height <= 0)
    {
        outRGBA.clear();
        return false;
    }

    const size_t need = (size_t)width * (size_t)height * 4u;
    outRGBA.resize(need);
    return grabToRGBA8(camIdx, width, height, outRGBA.data(), outRGBA.size());
}

bool MilManager::grabToRGBA8(int camIdx, int width, int height, uint8_t* outRGBA, size_t outBytes)
{
    if (!outRGBA) return false;
    if (width <= 0 || height <= 0) return false;

    const size_t need = (size_t)width * (size_t)height * 4u;
    if (outBytes < need) return false;

#if !defined(HAVE_MIL)
    std::memset(outRGBA, 0, need);
    return false;
#else
    std::lock_guard<std::recursive_mutex> lk(_mtx);


    if (!allocDig(camIdx))
        return false;

    auto& d = _digs[camIdx];

    // Allocate/reallocate 8-bit mono grab buffer
    if (d.grabBuf == M_NULL || d.w != width || d.h != height)
    {
        if (d.grabBuf != M_NULL) { MbufFree(d.grabBuf); d.grabBuf = M_NULL; }

        MIL_ID buf = M_NULL;
        MbufAlloc2d(_sysId, width, height, 8 + M_UNSIGNED, M_IMAGE + M_GRAB, &buf);
        if (buf == M_NULL)
        {
            setErr(*this, _lastError, "MbufAlloc2d failed.");
            return false;
        }
        d.grabBuf = buf;
        d.w = width;
        d.h = height;
    }

    // Correct MIL signature: MdigGrab(DigId, BufId)
    MdigGrab(d.dig, d.grabBuf);
    MdigGrabWait(d.dig, M_GRAB_END);

    std::vector<uint8_t> gray((size_t)width * (size_t)height);
    MbufGet2d(d.grabBuf, 0, 0, width, height, gray.data());

    grayToRGBA(gray.data(), width, height, outRGBA);
    setErr(*this, _lastError, "");
    return true;
#endif
}

static std::string milStringToStd(const std::wstring& s)
{
    std::string out;
    out.reserve(s.size());
    for (wchar_t ch : s)
        out.push_back((ch <= 0x7F) ? static_cast<char>(ch) : '?'); // ASCII-safe
    return out;
}

// Overload for narrow strings
static std::string milStringToStd(const std::string& s)
{
    return s;
}

std::string MilManager::diagnostics_NoLock() const
{
#if !defined(HAVE_MIL)
    return "MIL diagnostics: HAVE_MIL not defined.\n";
#else
    std::ostringstream oss;

    oss << "---- MIL Diagnostics ----\n";
    oss << "app=" << (_appId != M_NULL ? "ok" : "null") << "\n";
    oss << "sys=" << (_sysId != M_NULL ? "ok" : "null") << "\n";
    //oss << "cachedSysDesc=" << (_cachedSysDesc.empty() ? MIL_TEXT("(empty)") : _cachedSysDesc) << "\n";
    oss << "cachedSysDevNum=" << _cachedSysDevNum << "\n";

    MIL_INT sysCount = 0;
    MappInquire(M_DEFAULT, M_INSTALLED_SYSTEM_COUNT, &sysCount);
    oss << "installedSystems=" << sysCount << "\n";
    for (MIL_INT i = 0; i < sysCount; ++i)
    {
        MIL_STRING desc;
        MappInquire(M_DEFAULT, M_INSTALLED_SYSTEM_DESCRIPTOR + i, desc);
        oss << "  [" << i << "] " << milStringToStd(desc) << "\n";
    }

    // If system allocated, probe digitizer allocation results too
    if (_sysId != M_NULL)
    {
        oss << "validDigDevs=" << _validDigDevs.size() << "\n";
        for (int k = 0; k < (int)_validDigDevs.size(); ++k)
        {
            oss << "  camIdx " << k << " -> M_DEV" << (int)(_validDigDevs[k] - M_DEV0) << "\n";
        }

        if (!_lastProbeReport.empty())
        {
            oss << "\n_lastProbeReport:\n" << _lastProbeReport << "\n";
        }

        // Always probe a fixed range and record alloc success/fail
        oss << "\nProbe MdigAlloc M_DEV0..M_DEV15:\n";
        for (int i = 0; i < 16; ++i)
        {
            MIL_ID d = M_NULL;
            MIL_INT dev = M_DEV0 + i;
            MdigAlloc(_sysId, dev, MIL_TEXT("M_DEFAULT"), M_DEFAULT, &d);

            if (d == M_NULL)
                oss << "  M_DEV" << i << ": FAIL\n";
            else
            {
                oss << "  M_DEV" << i << ": OK\n";
                MdigFree(d);
            }
        }
    }

    oss << "-------------------------\n";
    return oss.str();
#endif
}

bool MilManager::grabGridToRGBA8(int gridCols, int gridRows, int tileW, int tileH, uint8_t* outRGBA, size_t outBytes)
{
    if (!outRGBA) return false;
    if (gridCols <= 0 || gridRows <= 0 || tileW <= 0 || tileH <= 0) return false;

    const int outW = gridCols * tileW;
    const int outH = gridRows * tileH;
    const size_t need = (size_t)outW * (size_t)outH * 4u;
    if (outBytes < need) return false;

    std::memset(outRGBA, 0, need);

    std::vector<uint8_t> tile;
    tile.resize((size_t)tileW * (size_t)tileH * 4u);

    for (int r = 0; r < gridRows; ++r)
    {
        for (int c = 0; c < gridCols; ++c)
        {
            int camIdx = r * gridCols + c;

            if (!grabToRGBA8(camIdx, tileW, tileH, tile.data(), tile.size()))
                continue;

            for (int y = 0; y < tileH; ++y)
            {
                const size_t srcOff = (size_t)y * (size_t)tileW * 4u;
                const size_t dstOff = ((size_t)(r * tileH + y) * (size_t)outW + (size_t)(c * tileW)) * 4u;
                std::memcpy(outRGBA + dstOff, tile.data() + srcOff, (size_t)tileW * 4u);
            }
        }
    }
    return true;
}

bool MilManager::grabGridToRGBA8(int gridCols, int gridRows, int tileW, int tileH, std::vector<uint8_t>& outRGBA)
{
    const int outW = gridCols * tileW;
    const int outH = gridRows * tileH;
    outRGBA.resize((size_t)outW * (size_t)outH * 4u);
    return grabGridToRGBA8(gridCols, gridRows, tileW, tileH, outRGBA.data(), outRGBA.size());
}
