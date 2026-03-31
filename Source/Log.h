#pragma once
#include <JuceHeader.h>
#include <cstdio>
#include <cstdarg>

// Global log file pointer -- set by Main.cpp before any logging.
// All modules write through logWrite().
inline FILE* g_logFile { nullptr };

inline void logWrite (const char* format, ...) noexcept
{
    if (g_logFile != nullptr)
    {
        va_list args;
        va_start (args, format);
        vfprintf (g_logFile, format, args);
        va_end (args);
        fflush (g_logFile);
    }
}
