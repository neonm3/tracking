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

#include <string>
#include <vector>

class MilManager
{
public:
    MilManager();
    ~MilManager();

    // Call once (or lazily). Returns false on failure; check lastError().
    bool init(const std::string& dcfPathUtf8, int deviceIndex);

    void shutdown();

    bool isReady() const { return m_ready; }

    // Grabs a frame into internal MIL buffer and copies to outRGBA (size = w*h*4).
    // Returns false on failure.
    bool grabToRGBA(std::vector<unsigned char>& outRGBA);

    int width() const { return (int)m_sizeX; }
    int height() const { return (int)m_sizeY; }
    int bands() const { return (int)m_bands; } // should be 1 for Mono8

    const std::string& lastError() const { return m_lastError; }

private:
#ifdef HAVE_MIL
    MIL_ID m_app = M_NULL;
    MIL_ID m_sys = M_NULL;
    MIL_ID m_dig = M_NULL;
    MIL_ID m_bufMono = M_NULL;
#endif

    MIL_INT m_sizeX = 0;
    MIL_INT m_sizeY = 0;
    MIL_INT m_bands = 0;

    bool m_ready = false;
    std::string m_lastError;

private:
#ifdef HAVE_MIL
    void setMilError(const char* where);
    // If you want DCF support later: implement UTF8->MIL_TEXT conversion here.
#endif
};
