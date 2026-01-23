#include "MilManager.h"
#include <algorithm>
#include <cstring>
#include <sstream>

static inline void setErr(std::string& dst, const std::string& msg)
{
	dst = msg;
}

template <typename T>
static std::string toString(const T& value)
{
	std::ostringstream oss;
	oss << value;
	return oss.str();
}

static void blitTile(std::vector<uint8_t>& outRGBA, int outWidth, int gridCols, int camIdx,
                     const std::vector<uint8_t>& src, int srcW, int srcH)
{
	const int col = camIdx % gridCols;
	const int row = camIdx / gridCols;
	const int dstX0 = col * srcW;
	const int dstY0 = row * srcH;
	for (int y = 0; y < srcH; y++)
	{
		uint8_t* dstLine = outRGBA.data() + ((size_t)(dstY0 + y) * outWidth + dstX0) * 4;
		const uint8_t* srcLine = src.data() + (size_t)y * srcW * 4;
		std::memcpy(dstLine, srcLine, (size_t)srcW * 4);
	}
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
		MdigAlloc(_sysId, dev, M_DEFAULT, M_DEFAULT, &dig);
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
		MdigAlloc(_sysId, deviceNum, M_DEFAULT, M_DEFAULT, &digId);
	}
	else
	{
		// Runtime string to MIL_STRING
		#if M_MIL_UNICODE_API
			std::wstring w(dcfPath.begin(), dcfPath.end());
			MdigAlloc(_sysId, deviceNum, w.c_str(), M_DEFAULT, &digId);
		#else
			MdigAlloc(_sysId, deviceNum, dcfPath.c_str(), M_DEFAULT, &digId);
		#endif
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
		MbufGetColor2d(d.grabBuf, M_RED,   0,0,w,h, b0.data());
		MbufGetColor2d(d.grabBuf, M_GREEN, 0,0,w,h, b1.data());
		MbufGetColor2d(d.grabBuf, M_BLUE,  0,0,w,h, b2.data());
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

bool MilManager::grabGridToRGBA8(int cameraCount, int gridCols, int deviceOffset, const std::string& dcfPath,
                                 std::vector<uint8_t>& outRGBA, int& outWidth, int& outHeight)
{
	// Avoid std::clamp to stay compatible with older MSVC language modes.
	if (cameraCount < 1) cameraCount = 1;
	if (cameraCount > 24) cameraCount = 24;
	gridCols = std::max(1, gridCols);
	const int gridRows = (cameraCount + gridCols - 1) / gridCols;

	// Grab first camera to establish tile size
	int tileW=0, tileH=0;
	std::vector<uint8_t> tile;
	if (!ensureDigitizer(deviceOffset, dcfPath))
		return false;
	if (!grabToRGBA8(deviceOffset, tile, tileW, tileH))
		return false;

	outWidth = tileW * gridCols;
	outHeight = tileH * gridRows;
	outRGBA.assign((size_t)outWidth*outHeight*4, 0);

	// Copy first tile already grabbed.
	blitTile(outRGBA, outWidth, gridCols, 0, tile, tileW, tileH);

	// Grab remaining cameras
	for (int i=1; i<cameraCount; i++)
	{
		const int dev = deviceOffset + i;
		if (!ensureDigitizer(dev, dcfPath))
			continue;

		int w=0,h=0;
		std::vector<uint8_t> frame;
		if (!grabToRGBA8(dev, frame, w, h))
			continue;

		// If a camera has different resolution, skip it for now.
		if (w != tileW || h != tileH)
			continue;

		blitTile(outRGBA, outWidth, gridCols, i, frame, w, h);
	}

	return true;
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
