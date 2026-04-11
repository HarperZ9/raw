#pragma once
//=============================================================================
//  ENBLog.h — File + Debug Output Logging
//
//  Logs to both OutputDebugString and enbseries.log in the game directory.
//=============================================================================

#include <Windows.h>
#include <cstdio>
#include <cstdarg>

class ENBLogger
{
public:
    void Initialize(const char* gameDir)
    {
        char path[MAX_PATH];
        snprintf(path, MAX_PATH, "%s\\enbseries.log", gameDir);
        m_file = fopen(path, "w");
        if (m_file)
            fprintf(m_file, "ENBSeries v0.492 for GTA V (rebuilt from source)\n\n");
    }

    void Shutdown()
    {
        if (m_file) { fclose(m_file); m_file = nullptr; }
    }

    void Log(const char* fmt, ...)
    {
        char buf[2048];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        OutputDebugStringA(buf);
        if (m_file)
        {
            fputs(buf, m_file);
            fflush(m_file);
        }
    }

private:
    FILE* m_file = nullptr;
};

extern ENBLogger g_Log;
