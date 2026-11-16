#include "Launcher.h"
#include "generated/Generated.h"

#if JUCE_MAC
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#endif

Launcher::Launcher (const juce::String& newCommFile, const juce::StringArray& newLaunchTarget)
    : commFile (newCommFile), launchTarget (newLaunchTarget)
{
#if JUCE_WINDOWS
    for (auto& element : launchTarget)
        if (element.containsChar (' ') and not element.startsWithChar ('"'))
            element = element.quoted ();
#endif
}

void Launcher::sendPid (std::uint32_t processId)
{
    std::string json;

    jam::JsonWriter::appendObject (json, [processId] (std::string& members)
    {
        jam::JsonWriter::appendKey (members, Id::kind);
        jam::JsonWriter::appendString (members, jam::Strings::getView (Id::pid.toString ()));
        jam::JsonWriter::appendKey (members, Id::pid);
        jam::Format::appendDecimal (members, static_cast<int> (processId));
    });

    json += '\n';
    writeLine (json);
}

void Launcher::sendError (const juce::String& text)
{
    std::string json;

    jam::JsonWriter::appendObject (json, [text] (std::string& members)
    {
        jam::JsonWriter::appendKey (members, Id::kind);
        jam::JsonWriter::appendString (members, jam::Strings::getView (Id::error.toString ()));
        jam::JsonWriter::appendKey (members, Id::error);
        jam::JsonWriter::appendString (members, jam::Strings::getView (text));
    });

    json += '\n';
    writeLine (json);
}

void Launcher::writeLine (const std::string& line)
{
#if JUCE_WINDOWS
    DWORD bytesWritten { 0 };
    const BOOL isWritten { WriteFile (pipe.get (), line.data (), static_cast<DWORD> (line.size ()), &bytesWritten, nullptr) };

    jassert (isWritten);
    juce::ignoreUnused (isWritten);
#endif

#if JUCE_MAC
    const int writeFifo { open (juce::File (commFile).getFullPathName ().toRawUTF8 (), O_WRONLY) };

    jassert (writeFifo >= 0);

    const auto bytesWritten { write (writeFifo, line.data (), line.size ()) };

    jassert (bytesWritten == static_cast<decltype (bytesWritten)> (line.size ()));
    juce::ignoreUnused (bytesWritten);
    close (writeFifo);
#endif
}

std::string Launcher::getLine ()
{
    std::string line;

#if JUCE_WINDOWS
    char character { 0 };
    DWORD bytesRead { 0 };

    while (ReadFile (pipe.get (), &character, 1, &bytesRead, nullptr) and bytesRead == 1 and character != '\n')
        line.push_back (character);
#endif

#if JUCE_MAC
    for (int character { fgetc (readFifo.get ()) };
         character != '\n' and (character != EOF or (ferror (readFifo.get ()) != 0 and errno == EINTR));
         character = fgetc (readFifo.get ()))
    {
        if (character == EOF)
            clearerr (readFifo.get ());
        else
            line.push_back (static_cast<char> (character));
    }
#endif

    return line;
}

bool Launcher::isAttached (const std::string& line)
{
    const auto document (jam::JsonDocument::parse (std::string_view { line }));

    if (document != nullptr)
    {
        const auto* type { document->find (*document->getRoot ()->firstChild, jam::Strings::getView (Id::kind.toString ())) };

        return type != nullptr and juce::Identifier { jam::Strings::fromView (document->getString (*type)) } == Id::didAttach;
    }

    return false;
}

#if JUCE_WINDOWS
int Launcher::run ()
{
    pipe.reset (CreateFileW (commFile.toWideCharPointer (), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr));
    jassert (pipe.get () != INVALID_HANDLE_VALUE);
    std::wstring commandLine { launchTarget.joinIntoString (" ").toWideCharPointer () };
    STARTUPINFOW startupInfo { sizeof (STARTUPINFOW) };
    PROCESS_INFORMATION processInformation {};
    DWORD exitCode { static_cast<DWORD> (failedLaunchExitCode) };

    if (CreateProcessW (nullptr, commandLine.data (), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, nullptr, &startupInfo, &processInformation))
    {
        const std::unique_ptr<void, decltype (&CloseHandle)> process { processInformation.hProcess, &CloseHandle };
        const std::unique_ptr<void, decltype (&CloseHandle)> thread { processInformation.hThread, &CloseHandle };

        sendPid (processInformation.dwProcessId);

        if (isAttached (getLine ()))
        {
            WaitForSingleObject (process.get (), INFINITE);
            GetExitCodeProcess (process.get (), &exitCode);
        }
        else
        {
            TerminateProcess (process.get (), exitCode);
        }
    }
    else
    {
        sendError ("Failed to launch target process");
    }

    return static_cast<int> (exitCode);
}
#endif

#if JUCE_MAC
int Launcher::run ()
{
    const juce::File readFile { juce::File (commFile).withFileExtension (Id::didAttach.toString ()) };
    readFifo.reset (fdopen (open (readFile.getFullPathName ().toRawUTF8 (), O_RDWR), "r"));
    std::vector<char*> argumentPointers;

    for (const auto& element : launchTarget)
        argumentPointers.push_back (const_cast<char*> (element.toRawUTF8 ()));

    argumentPointers.push_back (nullptr);
    sendPid (static_cast<std::uint32_t> (getpid ()));

    if (isAttached (getLine ()))
    {
        readFifo.reset ();
        execvp (argumentPointers.front (), argumentPointers.data ());
        std::fprintf (stderr, "%s\n", strerror (errno));
    }

    return failedLaunchExitCode;
}
#endif
