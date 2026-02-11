#include "MilManager.h"
#include <algorithm>
#include <cstring>
#include <sstream>

static inline void setErr(std::string& dst, const std::string& msg)
{
	dst = msg;
}

#if defined(HAVE_MIL)
static std::basic_string<MIL_TEXT_CHAR> toMilText(const std::string& src)
{
	std::basic_string<MIL_TEXT_CHAR> out;
	out.reserve(src.size());
	for (size_t i = 0; i < src.size(); i++)
	{
		out.push_back(static_cast<MIL_TEXT_CHAR>(src[i]));
	}
	return out;
}

static void bufGetColor2d(MIL_ID buf, MIL_INT band, MIL_INT x, MIL_INT y,
                          MIL_INT sizeX, MIL_INT sizeY, void* dst)
{
	MIL_ID child = M_NULL;
	MbufChildColor(buf, band, &child);
	if (!child)
		return;
	MbufGet2d(child, x, y, sizeX, sizeY, dst);
	MbufFree(child);
}
#endif

template <typename T>
static std::string toString(const T& value)
{
	std::ostringstream oss;
	oss << value;
	return oss.str();
}

MilManager::MilManager()
#if defined(HAVE_MIL)
	: _appId(M_NULL)
	, _sysId(M_NULL)
#endif
{
}

MilManager::Dig::Dig()
	: allocated(false)
	, deviceNum(-1)
	, digId(M_NULL)
	, grabBuf(M_NULL)
	, sizeX(0)
	, sizeY(0)
	, bands(0)
{
}

MilManager& MilManager::instance()
{
	static MilManager g;
	return g;
}

bool MilManager::builtWithMil() const
{
#if defined(HAVE_MIL)
	return true;
#else
	return false;
#endif
}

bool MilManager::hasSystem() const
{
#if defined(HAVE_MIL)
	LockGuard lk(_mtx);
	return _sysId != M_NULL;
#else
	return false;
#endif
}

std::string MilManager::summaryLine() const
{
	LockGuard lk(_mtx);
#if !defined(HAVE_MIL)
	return "MIL: disabled at compile time (HAVE_MIL not defined)";
#else
	int allocated = 0;
	for (size_t i = 0; i < _digs.size(); i++)
	{
		if (_digs[i].allocated)
			allocated++;
	}
	std::string s = "MIL: compiled=yes";
	s += std::string(" app=") + (_appId ? "ok" : "no");
	s += std::string(" sys=") + (_sysId ? "ok" : "no");
	s += " digs_allocated=" + toString(allocated);
	return s;
#endif
}

std::string MilManager::dumpDevices(int maxDev, bool verbose)
{
	if (maxDev < 1) maxDev = 1;
	if (maxDev > 256) maxDev = 256;

	LockGuard lk(_mtx);

#if !defined(HAVE_MIL)
	return "Cannot dump devices: built without MIL (define HAVE_MIL).";
#else
	// Ensure system exists first.
	if (!ensureSystem())
	{
		return std::string("MIL system not available: ") + _lastError;
	}

	std::string out;
	out.reserve(4096);
	out += "MIL Digitizer Probe (MdigAlloc)\n";
	out += "System allocated: " + std::string(_sysId ? "yes" : "no") + "\n";
	out += "Probing indices 0.." + toString(maxDev - 1) + "\n\n";

	for (int dev = 0; dev < maxDev; dev++)
	{
		MIL_ID dig = M_NULL;
		// Try autoconfig first (M_DEFAULT).
		MdigAlloc(_sysId, dev, MIL_TEXT("M_DEFAULT"), M_DEFAULT, &dig);
		if (dig)
		{
			MIL_INT sx = 0, sy = 0, sb = 0;
			MdigInquire(dig, M_SIZE_X, &sx);
			MdigInquire(dig, M_SIZE_Y, &sy);
			MdigInquire(dig, M_SIZE_BAND, &sb);
			out += "[OK] dev=" + toString(dev) + " size=" + toString((int)sx) + "x" + toString((int)sy) + " bands=" + toString((int)sb) + "\n";
			if (verbose)
			{
				// Try to query a couple of common strings if available. Not all MIL builds support these.
				// Guard with try/catch not possible in C API; just best-effort.
			}
			MdigFree(dig);
		}
		else
		{
			out += "[--] dev=" + toString(dev) + " (alloc failed)\n";
		}
	}

	return out;
#endif
}

MilManager::~MilManager()
{
	shutdown();
}

std::string MilManager::lastError() const
{
	LockGuard lk(_mtx);
	return _lastError;
}

bool MilManager::ensureSystem()
{
#if !defined(HAVE_MIL)
	setErr(_lastError, "Built without MIL (define HAVE_MIL and add MIL include/lib paths).");
	return false;
#else
	if (_sysId)
		return true;

	// Allocate MIL application/system. We use default system, which should pick up installed Matrox drivers.
	MappAlloc(M_DEFAULT, &_appId);
	if (!_appId)
	{
		setErr(_lastError, "MappAlloc failed.");
		return false;
	}

	MsysAlloc(M_DEFAULT, M_SYSTEM_DEFAULT, M_DEFAULT, M_DEFAULT, &_sysId);
	if (!_sysId)
	{
		setErr(_lastError, "MsysAlloc failed (no MIL system found?).");
		return false;
	}

	return true;
#endif
}

bool MilManager::allocDig(Dig& d, int deviceNum, const std::string& dcfPath)
{
#if !defined(HAVE_MIL)
	(void)d; (void)deviceNum; (void)dcfPath;
	return false;
#else
	if (!ensureSystem())
		return false;

	// Allocate digitizer.
	// Note: MIL_TEXT only wraps string literals; if you pass a runtime path, use MIL_STRING conversion.
	// We support runtime path below.
	MIL_ID digId = M_NULL;

	if (dcfPath.empty())
	{
		MdigAlloc(_sysId, deviceNum, MIL_TEXT("M_DEFAULT"), M_DEFAULT, &digId);
	}
	else
	{
		std::basic_string<MIL_TEXT_CHAR> dcfText = toMilText(dcfPath);
		MdigAlloc(_sysId, deviceNum, dcfText.c_str(), M_DEFAULT, &digId);
	}

	if (!digId)
	{
		setErr(_lastError, "MdigAlloc failed for device " + toString(deviceNum));
		return false;
	}

	// Inquire sizes
	MIL_INT sx = 0, sy = 0, sb = 0;
	MdigInquire(digId, M_SIZE_X, &sx);
	MdigInquire(digId, M_SIZE_Y, &sy);
	MdigInquire(digId, M_SIZE_BAND, &sb);

	// Allocate grab buffer (we store as 8-bit monochrome or 8-bit x3; MIL will adapt to camera's pixel format).
	// For safest CPU copy, use M_IMAGE + M_GRAB + M_PROC.
	MIL_ID buf = M_NULL;
	// Allocate as 3-band 8-bit by default; if camera is monochrome, band count may still be 1.
	const MIL_INT bands = std::max<MIL_INT>(1, sb);
	MbufAllocColor(_sysId, bands, sx, sy, 8 + M_UNSIGNED, M_IMAGE + M_GRAB + M_PROC, &buf);
	if (!buf)
	{
		MdigFree(digId);
		setErr(_lastError, "MbufAllocColor failed for device " + toString(deviceNum));
		return false;
	}

	d.allocated = true;
	d.deviceNum = deviceNum;
	d.dcfPath = dcfPath;
	d.digId = digId;
	d.grabBuf = buf;
	d.sizeX = sx;
	d.sizeY = sy;
	d.bands = bands;

	return true;
#endif
}

void MilManager::freeDig(Dig& d)
{
#if defined(HAVE_MIL)
	if (d.grabBuf) { MbufFree(d.grabBuf); d.grabBuf = M_NULL; }
	if (d.digId) { MdigFree(d.digId); d.digId = M_NULL; }
#endif
	d = Dig();
}

bool MilManager::ensureDigitizer(int deviceNum, const std::string& dcfPath)
{
	LockGuard lk(_mtx);

	if (deviceNum < 0)
	{
		setErr(_lastError, "Invalid device number.");
		return false;
	}

	if ((int)_digs.size() <= deviceNum)
		_digs.resize(deviceNum + 1);

	Dig& d = _digs[deviceNum];
	if (d.allocated)
	{
		// Re-alloc if DCF changed.
		if (d.dcfPath != dcfPath)
		{
			freeDig(d);
		}
		else
			return true;
	}

	return allocDig(d, deviceNum, dcfPath);
}

bool MilManager::grabToRGBA8(int deviceNum, std::vector<uint8_t>& outRGBA, int& outWidth, int& outHeight)
{
	LockGuard lk(_mtx);

	if (deviceNum < 0 || deviceNum >= (int)_digs.size() || !_digs[deviceNum].allocated)
	{
		setErr(_lastError, "Digitizer not allocated for device " + toString(deviceNum));
		return false;
	}

#if !defined(HAVE_MIL)
	setErr(_lastError, "Built without MIL.");
	return false;
#else
	Dig& d = _digs[deviceNum];

	// Single-frame grab (blocking).
	MdigGrab(d.digId, d.grabBuf);

	const int w = (int)d.sizeX;
	const int h = (int)d.sizeY;
	outWidth = w;
	outHeight = h;
	outRGBA.resize((size_t)w * (size_t)h * 4);

	// Fetch into temporary band buffers then interleave RGBA.
	// For performance, you may want to allocate persistent temp buffers per digitizer.
	std::vector<uint8_t> b0((size_t)w*h);
	std::vector<uint8_t> b1, b2;
	if (d.bands >= 3)
	{
		b1.resize((size_t)w*h);
		b2.resize((size_t)w*h);
	}

	if (d.bands == 1)
	{
		MbufGet2d(d.grabBuf, 0, 0, w, h, b0.data());
		for (size_t i = 0, n = (size_t)w*h; i < n; i++)
		{
			const uint8_t v = b0[i];
			outRGBA[i*4+0] = v;
			outRGBA[i*4+1] = v;
			outRGBA[i*4+2] = v;
			outRGBA[i*4+3] = 255;
		}
	}
	else
	{
		// Get each band
		bufGetColor2d(d.grabBuf, M_RED,   0, 0, w, h, b0.data());
		bufGetColor2d(d.grabBuf, M_GREEN, 0, 0, w, h, b1.data());
		bufGetColor2d(d.grabBuf, M_BLUE,  0, 0, w, h, b2.data());
		for (size_t i = 0, n = (size_t)w*h; i < n; i++)
		{
			outRGBA[i*4+0] = b0[i];
			outRGBA[i*4+1] = b1[i];
			outRGBA[i*4+2] = b2[i];
			outRGBA[i*4+3] = 255;
		}
	}

	return true;
#endif
}

void MilManager::shutdown()
{
	LockGuard lk(_mtx);
	for (size_t i = 0; i < _digs.size(); i++)
	{
		if (_digs[i].allocated)
			freeDig(_digs[i]);
	}
	_digs.clear();

#if defined(HAVE_MIL)
	if (_sysId) { MsysFree(_sysId); _sysId = M_NULL; }
	if (_appId) { MappFree(_appId); _appId = M_NULL; }
#endif
}
