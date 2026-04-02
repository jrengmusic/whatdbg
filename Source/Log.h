#pragma once
#include <JuceHeader.h>

#if JUCE_DEBUG

#include <cstdio>
#include <cstdarg>

/** Global log file pointer set by Main.cpp before any logging occurs.
 *
 *  All modules write through logWrite(). Must be non-null before the first
 *  call to logWrite or output is silently dropped.
 *
 *  @note Set once at startup on the main thread. Not thread-safe for concurrent writes.
 */
inline FILE* g_logFile { nullptr };

/** Write a formatted message to the global log file.
 *
 *  Accepts printf-style format string and variadic arguments. Flushes
 *  the file after every write so that log output is visible even on crash.
 *  A no-op when g_logFile is null or in release builds.
 *
 *  @param format  printf-style format string.
 *  @param ...     Variadic arguments matching the format string.
 *
 *  @note Only compiled in JUCE_DEBUG builds. Release builds compile this
 *        to an empty inline that the optimizer eliminates entirely.
 */
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

#else

/** No-op release build stub — compiled away by the optimizer. */
inline void logWrite (const char*, ...) noexcept {}

#endif
