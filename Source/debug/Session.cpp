#include <JuceHeader.h>
#include "Session.h"
#include "../Log.h"
#include <dbghelp.h>

namespace debug
{

using DynObj = juce::ReferenceCountedObjectPtr<juce::DynamicObject>;

static juce::String formatSymbolValue (const juce::String& rawValue) noexcept
{
    juce::String result { rawValue };

    // Clean 64-bit pointer backtick: "0x00000000`10db01b0" → "0x0000000010db01b0"
    result = result.replace ("`", "");

    // Strip dbgeng decimal prefix "0n" when followed by a digit (anywhere in string).
    // "0n877" → "877", "mid (0n2)" → "mid (2)"
    int searchPos { 0 };
    while (searchPos < result.length () - 2)
    {
        const int found { result.indexOf (searchPos, "0n") };

        if (found < 0)
        {
            searchPos = result.length ();
        }
        else
        {
            const int charAfter { found + 2 };

            if (charAfter < result.length () and juce::CharacterFunctions::isDigit (result[charAfter]))
            {
                result = result.substring (0, found) + result.substring (charAfter);
            }
            else
            {
                searchPos = found + 2;
            }
        }
    }

    // Pointer with trailing type: "0x0000000010550c30 class Foo *" → "0x0000000010550c30"
    if (result.startsWith ("0x"))
    {
        const int spacePos { result.indexOf (" ") };

        if (spacePos > 0)
        {
            result = result.substring (0, spacePos);
        }
    }

    // Composite type echoed as value: "class juce::String" or "struct Foo" → empty
    // The type column already identifies it; expand triangle indicates expandability.
    if (result.startsWith ("class ") or result.startsWith ("struct "))
    {
        result = "";
    }

    return result;
}

// Read a null-terminated string from target process memory at the given address.
static juce::String readTargetString (IDebugDataSpaces4* dataSpaces, ULONG64 address) noexcept
{
    juce::String result;

    if (dataSpaces != nullptr and address != 0)
    {
        static constexpr ULONG kMaxStringReadSize { 256 };
        char buffer[kMaxStringReadSize] {};
        ULONG bytesRead { 0 };

        const HRESULT hr { dataSpaces->ReadMultiByteStringVirtual (
            address, kMaxStringReadSize, buffer, kMaxStringReadSize, &bytesRead) };

        if (SUCCEEDED (hr) and bytesRead > 0)
        {
            result = juce::String (buffer);
        }
    }

    return result;
}

// Extract a hex address from a symbol value text like "0x0000000010db01b0"
static ULONG64 parseHexAddress (const juce::String& valueText) noexcept
{
    ULONG64 address { 0 };

    const juce::String cleaned { valueText.replace ("`", "").trim () };

    if (cleaned.startsWith ("0x"))
    {
        const juce::String hexPart { cleaned.substring (2).upToFirstOccurrenceOf (" ", false, false) };
        address = static_cast<ULONG64> (hexPart.getHexValue64 ());
    }

    return address;
}

// Find a child symbol by name within an expanded parent.
// Returns the child's index, or -1 if not found.
static int findChildByName (IDebugSymbolGroup2* group, ULONG parentIndex,
                            ULONG totalCount, const char* childName) noexcept
{
    int foundIndex { -1 };

    for (ULONG i { parentIndex + 1 }; i < totalCount; ++i)
    {
        DEBUG_SYMBOL_PARAMETERS childParams {};
        const HRESULT hr { group->GetSymbolParameters (i, 1, &childParams) };

        if (SUCCEEDED (hr))
        {
            if (childParams.ParentSymbol == parentIndex)
            {
                static constexpr int kChildNameSize { 256 };
                char nameBuffer[kChildNameSize] {};
                group->GetSymbolName (i, nameBuffer, kChildNameSize, nullptr);

                if (strcmp (nameBuffer, childName) == 0)
                {
                    foundIndex = static_cast<int> (i);
                }
            }
        }

        if (foundIndex >= 0)
        {
            i = totalCount;
        }
    }

    return foundIndex;
}

// Get the value text of a symbol at a given index.
static juce::String getChildValueText (IDebugSymbolGroup2* group, int index) noexcept
{
    juce::String result;

    if (index >= 0)
    {
        static constexpr int kValueSize { 512 };
        char buffer[kValueSize] {};
        const HRESULT hr { group->GetSymbolValueText (
            static_cast<ULONG> (index), buffer, kValueSize, nullptr) };

        if (SUCCEEDED (hr))
        {
            result = juce::String (buffer);
        }
    }

    return result;
}

// Pretty-print a known type. Returns empty string if type is not recognized.
// Uses a temporary symbol group — caller's group is NOT modified.
static juce::String prettyPrint (IDebugSymbols3* symbols, IDebugDataSpaces4* dataSpaces,
                                 int frameIndex, int symbolIndex,
                                 const juce::String& typeName) noexcept
{
    juce::String result;

    if (symbols != nullptr and dataSpaces != nullptr)
    {
    // ── juce::String ──────────────────────────────────────────────────
    // Layout: String → text (CharPointer_UTF8) → data (char*)
    if (typeName.contains ("juce::String"))
    {
        symbols->SetScopeFrameByIndex (static_cast<ULONG> (frameIndex));

        IDebugSymbolGroup2* group { nullptr };
        const HRESULT hr { symbols->GetScopeSymbolGroup2 (DEBUG_SCOPE_GROUP_ALL, nullptr, &group) };

        if (SUCCEEDED (hr) and group != nullptr)
        {
            group->ExpandSymbol (static_cast<ULONG> (symbolIndex), TRUE);
            ULONG count { 0 };
            group->GetNumberSymbols (&count);

            const int textIdx { findChildByName (group, static_cast<ULONG> (symbolIndex), count, "text") };

            if (textIdx >= 0)
            {
                group->ExpandSymbol (static_cast<ULONG> (textIdx), TRUE);
                group->GetNumberSymbols (&count);

                const int dataIdx { findChildByName (group, static_cast<ULONG> (textIdx), count, "data") };

                if (dataIdx >= 0)
                {
                    const juce::String dataValue { getChildValueText (group, dataIdx) };
                    const ULONG64 address { parseHexAddress (dataValue) };

                    if (address != 0)
                    {
                        const juce::String content { readTargetString (dataSpaces, address) };
                        result = "\"" + content + "\"";
                    }
                }
            }

            group->Release ();
        }

        symbols->ResetScope ();
    }

    // ── std::basic_string<char> (MSVC STL) ────────────────────────────
    // Layout: _Mypair._Myval2._Mysize for length
    //         _Mypair._Myval2._Bx._Buf (SSO, size <= 15) or _Bx._Ptr (heap)
    else if (typeName.contains ("std::basic_string<char"))
    {
        symbols->SetScopeFrameByIndex (static_cast<ULONG> (frameIndex));

        IDebugSymbolGroup2* group { nullptr };
        const HRESULT hr { symbols->GetScopeSymbolGroup2 (DEBUG_SCOPE_GROUP_ALL, nullptr, &group) };

        if (SUCCEEDED (hr) and group != nullptr)
        {
            group->ExpandSymbol (static_cast<ULONG> (symbolIndex), TRUE);
            ULONG count { 0 };
            group->GetNumberSymbols (&count);

            const int mypairIdx { findChildByName (group, static_cast<ULONG> (symbolIndex), count, "_Mypair") };

            if (mypairIdx >= 0)
            {
                group->ExpandSymbol (static_cast<ULONG> (mypairIdx), TRUE);
                group->GetNumberSymbols (&count);

                const int myval2Idx { findChildByName (group, static_cast<ULONG> (mypairIdx), count, "_Myval2") };

                if (myval2Idx >= 0)
                {
                    group->ExpandSymbol (static_cast<ULONG> (myval2Idx), TRUE);
                    group->GetNumberSymbols (&count);

                    const int sizeIdx { findChildByName (group, static_cast<ULONG> (myval2Idx), count, "_Mysize") };
                    const juce::String sizeText { getChildValueText (group, sizeIdx) };
                    const int stringSize { sizeText.replace ("0n", "").getIntValue () };

                    const int bxIdx { findChildByName (group, static_cast<ULONG> (myval2Idx), count, "_Bx") };

                    if (bxIdx >= 0)
                    {
                        static constexpr int kSsoThreshold { 16 };

                        group->ExpandSymbol (static_cast<ULONG> (bxIdx), TRUE);
                        group->GetNumberSymbols (&count);

                        if (stringSize < kSsoThreshold)
                        {
                            const int bufIdx { findChildByName (group, static_cast<ULONG> (bxIdx), count, "_Buf") };
                            const juce::String bufValue { getChildValueText (group, bufIdx) };
                            const int quoteStart { bufValue.indexOf ("\"") };
                            const int quoteEnd { bufValue.lastIndexOf ("\"") };

                            if (quoteStart >= 0 and quoteEnd > quoteStart)
                            {
                                result = bufValue.substring (quoteStart, quoteEnd + 1);
                            }
                        }
                        else
                        {
                            const int ptrIdx { findChildByName (group, static_cast<ULONG> (bxIdx), count, "_Ptr") };
                            const juce::String ptrValue { getChildValueText (group, ptrIdx) };
                            const ULONG64 address { parseHexAddress (ptrValue) };

                            if (address != 0)
                            {
                                const juce::String content { readTargetString (dataSpaces, address) };
                                result = "\"" + content + "\"";
                            }
                        }
                    }
                }
            }

            group->Release ();
        }

        symbols->ResetScope ();
    }

    // ── std::unique_ptr<T> ────────────────────────────────────────────
    // Layout: _Mypair._Myval2 is the raw pointer
    else if (typeName.contains ("std::unique_ptr<"))
    {
        symbols->SetScopeFrameByIndex (static_cast<ULONG> (frameIndex));

        IDebugSymbolGroup2* group { nullptr };
        const HRESULT hr { symbols->GetScopeSymbolGroup2 (DEBUG_SCOPE_GROUP_ALL, nullptr, &group) };

        if (SUCCEEDED (hr) and group != nullptr)
        {
            group->ExpandSymbol (static_cast<ULONG> (symbolIndex), TRUE);
            ULONG count { 0 };
            group->GetNumberSymbols (&count);

            const int mypairIdx { findChildByName (group, static_cast<ULONG> (symbolIndex), count, "_Mypair") };

            if (mypairIdx >= 0)
            {
                group->ExpandSymbol (static_cast<ULONG> (mypairIdx), TRUE);
                group->GetNumberSymbols (&count);

                const int myval2Idx { findChildByName (group, static_cast<ULONG> (mypairIdx), count, "_Myval2") };
                const juce::String ptrValue { getChildValueText (group, myval2Idx) };
                const ULONG64 address { parseHexAddress (ptrValue) };

                if (address == 0)
                {
                    result = "null";
                }
                else
                {
                    result = "0x" + juce::String::toHexString (static_cast<juce::int64> (address));
                }
            }

            group->Release ();
        }

        symbols->ResetScope ();
    }

    // ── std::vector<T> ───────────────────────────────────────────────
    // Layout: _Mypair._Myval2._Myfirst (begin ptr), _Mylast (end ptr)
    else if (typeName.contains ("std::vector<"))
    {
        symbols->SetScopeFrameByIndex (static_cast<ULONG> (frameIndex));

        IDebugSymbolGroup2* group { nullptr };
        const HRESULT hr { symbols->GetScopeSymbolGroup2 (DEBUG_SCOPE_GROUP_ALL, nullptr, &group) };

        if (SUCCEEDED (hr) and group != nullptr)
        {
            group->ExpandSymbol (static_cast<ULONG> (symbolIndex), TRUE);
            ULONG count { 0 };
            group->GetNumberSymbols (&count);

            const int mypairIdx { findChildByName (group, static_cast<ULONG> (symbolIndex), count, "_Mypair") };

            if (mypairIdx >= 0)
            {
                group->ExpandSymbol (static_cast<ULONG> (mypairIdx), TRUE);
                group->GetNumberSymbols (&count);

                const int myval2Idx { findChildByName (group, static_cast<ULONG> (mypairIdx), count, "_Myval2") };

                if (myval2Idx >= 0)
                {
                    group->ExpandSymbol (static_cast<ULONG> (myval2Idx), TRUE);
                    group->GetNumberSymbols (&count);

                    const int firstIdx { findChildByName (group, static_cast<ULONG> (myval2Idx), count, "_Myfirst") };
                    const int lastIdx  { findChildByName (group, static_cast<ULONG> (myval2Idx), count, "_Mylast") };

                    if (firstIdx >= 0 and lastIdx >= 0)
                    {
                        const juce::String firstValue { getChildValueText (group, firstIdx) };
                        const juce::String lastValue  { getChildValueText (group, lastIdx) };

                        const ULONG64 firstAddr { parseHexAddress (firstValue) };
                        const ULONG64 lastAddr  { parseHexAddress (lastValue) };

                        if (firstAddr == 0 and lastAddr == 0)
                        {
                            result = "size=0";
                        }
                        else if (lastAddr >= firstAddr)
                        {
                            static constexpr int kTypeSize { 256 };
                            char elemTypeBuffer[kTypeSize] {};
                            group->GetSymbolTypeName (static_cast<ULONG> (firstIdx),
                                                     elemTypeBuffer, kTypeSize, nullptr);

                            juce::String elemType { elemTypeBuffer };

                            if (elemType.endsWith (" *"))
                            {
                                elemType = elemType.dropLastCharacters (2);
                            }

                            ULONG64 moduleBase { 0 };
                            ULONG typeId { 0 };
                            const HRESULT typeResult { symbols->GetSymbolTypeId (
                                elemType.toRawUTF8 (), &typeId, &moduleBase) };

                            if (SUCCEEDED (typeResult))
                            {
                                ULONG elemSize { 0 };
                                const HRESULT sizeResult { symbols->GetTypeSize (
                                    moduleBase, typeId, &elemSize) };

                                if (SUCCEEDED (sizeResult) and elemSize > 0)
                                {
                                    const ULONG64 byteCount { lastAddr - firstAddr };
                                    const ULONG64 elementCount { byteCount / elemSize };
                                    result = "size=" + juce::String (static_cast<juce::int64> (elementCount));
                                }
                            }

                            if (result.isEmpty () and lastAddr > firstAddr)
                            {
                                const ULONG64 byteCount { lastAddr - firstAddr };
                                result = "size=" + juce::String (static_cast<juce::int64> (byteCount)) + " bytes";
                            }
                        }
                    }
                }
            }

            group->Release ();
        }

        symbols->ResetScope ();
    }

    } // if (symbols != nullptr and dataSpaces != nullptr)

    return result;
}

Session::~Session ()
{
    shutdown ();
}

bool Session::initialize (const juce::File& sidecarDir) noexcept
{
    const HRESULT comResult { CoInitializeEx (nullptr, COINIT_MULTITHREADED) };
    const bool isComOk { comResult == S_OK or comResult == RPC_E_CHANGED_MODE };

    if (isComOk)
    {
        isComOwned = (comResult == S_OK);
        const bool isLoaderOk { loader.load (sidecarDir) };

        if (isLoaderOk)
        {
            IDebugClient5* rawClient { nullptr };
            const HRESULT createResult { loader.createDebugClient (&rawClient) };

            if (SUCCEEDED (createResult) and rawClient != nullptr)
            {
                client.Attach (rawClient);

                const HRESULT qiControlResult { client->QueryInterface (
                    __uuidof (IDebugControl4),
                    reinterpret_cast<PVOID*> (control.GetAddressOf ())) };

                if (SUCCEEDED (qiControlResult) and control != nullptr)
                {
                    const HRESULT cbResult { client->SetOutputCallbacks (
                        reinterpret_cast<IDebugOutputCallbacks*> (&outputCallbacks)) };
                    juce::ignoreUnused (cbResult);

                    client->SetOutputMask (
                        DEBUG_OUTPUT_NORMAL
                        | DEBUG_OUTPUT_WARNING
                        | DEBUG_OUTPUT_ERROR
                        | DEBUG_OUTPUT_DEBUGGEE);

                    const HRESULT qiSymbolsResult { client->QueryInterface (
                        __uuidof (IDebugSymbols3),
                        reinterpret_cast<PVOID*> (symbols.GetAddressOf ())) };
                    juce::ignoreUnused (qiSymbolsResult);

                    const HRESULT qiDataResult { client->QueryInterface (
                        __uuidof (IDebugDataSpaces4),
                        reinterpret_cast<PVOID*> (dataSpaces.GetAddressOf ())) };
                    juce::ignoreUnused (qiDataResult);

                    if (symbols != nullptr)
                    {
                        symbols->AddSymbolOptions (SYMOPT_LOAD_LINES);
                        control->SetCodeLevel (DEBUG_LEVEL_SOURCE);
                    }

                    control->AddEngineOptions (DEBUG_ENGOPT_INITIAL_BREAK);
                    client->SetEventCallbacks (&eventCallbacks);
                }
            }
        }
    }

    const bool isInitialized { client != nullptr and control != nullptr and symbols != nullptr and dataSpaces != nullptr };

    if (not isInitialized)
    {
        shutdown ();
        logWrite ("WHATDBG: initialization failed\n");
    }

    return isInitialized;
}

bool Session::launch (const juce::String& program) noexcept
{
    jassert (client != nullptr);

    juce::String normalized { program.replace ("/", "\\") };

    if (normalized.containsChar (' ') and not normalized.startsWithChar ('"'))
        normalized = "\"" + normalized + "\"";

    logWrite ("WHATDBG: CreateProcess2 commandLine: %s\n", normalized.toRawUTF8 ());

    std::string commandLineBuffer { normalized.toStdString () };

    DEBUG_CREATE_PROCESS_OPTIONS options {};
    options.CreateFlags    = DEBUG_ONLY_THIS_PROCESS | CREATE_NEW_CONSOLE;
    options.EngCreateFlags = 0;
    options.VerifierFlags  = 0;
    options.Reserved       = 0;

    const HRESULT result { client->CreateProcess2 (
        0,
        commandLineBuffer.data (),
        &options,
        sizeof (options),
        nullptr,
        nullptr) };

    const bool launched { SUCCEEDED (result) };

    if (launched)
    {
        logWrite ("WHATDBG: launched process: %s\n", program.toRawUTF8 ());
    }
    else
    {
        logWrite ("WHATDBG: CreateProcess2 failed, hr=0x%08lX\n", static_cast<unsigned long> (result));
    }

    return launched;
}

bool Session::attach (ULONG processId) noexcept
{
    jassert (client != nullptr);

    const HRESULT attachResult { client->AttachProcess (0, processId, 0) };
    const bool attached { SUCCEEDED (attachResult) };

    if (attached)
    {
        logWrite ("WHATDBG: attached to process %lu\n", static_cast<unsigned long> (processId));
    }
    else
    {
        logWrite ("WHATDBG: AttachProcess failed, hr=0x%08lX\n", static_cast<unsigned long> (attachResult));
    }

    return attached;
}

void Session::resume () noexcept
{
    if (control != nullptr)
    {
        control->SetExecutionStatus (DEBUG_STATUS_GO);
    }
}

HRESULT Session::pollEvents (ULONG timeoutMs) noexcept
{
    HRESULT result { E_FAIL };

    if (control != nullptr)
    {
        result = control->WaitForEvent (0, timeoutMs);
    }

    return result;
}

void Session::shutdown () noexcept
{
    if (client != nullptr)
    {
        client->EndSession (DEBUG_END_ACTIVE_DETACH);
    }

    dataSpaces.Reset ();
    symbols.Reset ();
    control.Reset ();
    client.Reset ();

    if (isComOwned)
    {
        CoUninitialize ();
        isComOwned = false;
    }
}

HRESULT Session::getOffsetByLine (const juce::String& filePath, ULONG line, ULONG64* outOffset) noexcept
{
    HRESULT result { E_FAIL };

    if (symbols != nullptr)
    {
        result = symbols->GetOffsetByLine (line, filePath.toRawUTF8 (), outOffset);
    }

    return result;
}

HRESULT Session::getLineByOffset (ULONG64 offset, juce::String& outFilePath, ULONG* outLine) noexcept
{
    HRESULT result { E_FAIL };

    if (symbols != nullptr)
    {
        char pathBuffer[MAX_PATH] {};
        ULONG pathSize { 0 };

        result = symbols->GetLineByOffset (offset, outLine, pathBuffer, MAX_PATH, &pathSize, nullptr);

        if (SUCCEEDED (result))
        {
            outFilePath = juce::String (pathBuffer);
        }
    }

    return result;
}

HRESULT Session::addBreakpoint (ULONG64 offset, ULONG* outEngineId) noexcept
{
    HRESULT result { E_FAIL };

    if (control != nullptr)
    {
        IDebugBreakpoint2* bp { nullptr };
        result = control->AddBreakpoint2 (DEBUG_BREAKPOINT_CODE, DEBUG_ANY_ID, &bp);

        if (SUCCEEDED (result) and bp != nullptr)
        {
            bp->SetOffset (offset);
            bp->AddFlags (DEBUG_BREAKPOINT_ENABLED);
            bp->GetId (outEngineId);
        }
    }

    return result;
}

HRESULT Session::removeBreakpoint (ULONG engineId) noexcept
{
    HRESULT result { E_FAIL };

    if (control != nullptr)
    {
        IDebugBreakpoint2* bp { nullptr };
        result = control->GetBreakpointById2 (engineId, &bp);

        if (SUCCEEDED (result) and bp != nullptr)
        {
            result = control->RemoveBreakpoint2 (bp);
        }
    }

    return result;
}


HRESULT Session::loadModuleSymbols (const juce::String& imageName) noexcept
{
    HRESULT result { E_FAIL };

    if (control != nullptr)
    {
        const juce::String basename { juce::File (imageName).getFileName () };
        const juce::String command { ".reload /f " + basename.quoted () };
        result = control->Execute (DEBUG_OUTCTL_IGNORE,
                                   command.toRawUTF8 (),
                                   DEBUG_EXECUTE_NOT_LOGGED);
        logWrite ("WHATDBG: .reload /f %s hr=0x%08lX\n",
                  basename.toRawUTF8 (),
                  static_cast<unsigned long> (result));
    }

    return result;
}

HRESULT Session::forceReloadAllSymbols () noexcept
{
    HRESULT result { E_FAIL };

    if (control != nullptr)
    {
        result = control->Execute (DEBUG_OUTCTL_IGNORE,
                                   ".reload /f",
                                   DEBUG_EXECUTE_NOT_LOGGED);
        logWrite ("WHATDBG: .reload /f (all) hr=0x%08lX\n", static_cast<unsigned long> (result));
    }

    return result;
}

void Session::stepOver () noexcept
{
    if (control != nullptr)
    {
        control->SetExecutionStatus (DEBUG_STATUS_STEP_OVER);
    }
}

void Session::stepInto () noexcept
{
    if (control != nullptr)
    {
        control->SetExecutionStatus (DEBUG_STATUS_STEP_INTO);
    }
}

void Session::stepOut () noexcept
{
    if (control != nullptr)
    {
        control->Execute (DEBUG_OUTCTL_IGNORE, "gu", DEBUG_EXECUTE_NOT_LOGGED);
    }
}

void Session::interrupt (ULONG processId) noexcept
{
    if (processId != 0)
    {
        const HANDLE handle { OpenProcess (PROCESS_ALL_ACCESS, FALSE, processId) };

        if (handle != nullptr)
        {
            const BOOL result { DebugBreakProcess (handle) };
            CloseHandle (handle);

            if (result)
            {
                logWrite ("WHATDBG: DebugBreakProcess success, PID=%lu\n",
                          static_cast<unsigned long> (processId));
            }
            else
            {
                logWrite ("WHATDBG: DebugBreakProcess failed, PID=%lu error=%lu\n",
                          static_cast<unsigned long> (processId), GetLastError ());
            }
        }
        else
        {
            logWrite ("WHATDBG: OpenProcess failed, PID=%lu error=%lu\n",
                      static_cast<unsigned long> (processId), GetLastError ());
        }
    }
}

void Session::appendSymbolPath (const juce::String& path) noexcept
{
    if (symbols != nullptr)
    {
        symbols->AppendSymbolPath (path.toRawUTF8 ());
        logWrite ("WHATDBG: appended symbol path: %s\n", path.toRawUTF8 ());
    }
}

void Session::appendSourcePath (const juce::String& path) noexcept
{
    if (symbols != nullptr)
    {
        symbols->AppendSourcePath (path.toRawUTF8 ());
        logWrite ("WHATDBG: appended source path: %s\n", path.toRawUTF8 ());
    }
}

juce::Array<juce::var> Session::getStackTrace (int maxFrames) noexcept
{
    juce::Array<juce::var> frames;

    if (control != nullptr and symbols != nullptr)
    {
        static constexpr int kMaxStackFrames { 128 };
        static constexpr int kNameBufferSize { 512 };
        static constexpr int kFileBufferSize { 1024 };
        const int frameCount { juce::jmin (maxFrames, kMaxStackFrames) };

        std::vector<DEBUG_STACK_FRAME> stackFrames (static_cast<size_t> (frameCount));
        ULONG framesFilled { 0 };

        const HRESULT hr { control->GetStackTrace (
            0, 0, 0,
            stackFrames.data (),
            static_cast<ULONG> (frameCount),
            &framesFilled) };

        if (SUCCEEDED (hr))
        {
            for (ULONG i { 0 }; i < framesFilled; ++i)
            {
                DynObj frame { new juce::DynamicObject () };
                frame->setProperty ("id", static_cast<int> (i));
                frame->setProperty ("name", "frame");

                // Resolve function name
                char nameBuffer[kNameBufferSize] {};
                ULONG nameSize { 0 };
                ULONG64 displacement { 0 };

                const HRESULT nameResult { symbols->GetNameByOffset (
                    stackFrames.at (static_cast<size_t> (i)).InstructionOffset,
                    nameBuffer,
                    kNameBufferSize,
                    &nameSize,
                    &displacement) };

                if (SUCCEEDED (nameResult))
                {
                    frame->setProperty ("name", juce::String (nameBuffer));
                }

                // Resolve source location
                char fileBuffer[kFileBufferSize] {};
                ULONG fileSize { 0 };
                ULONG line { 0 };

                const HRESULT lineResult { symbols->GetLineByOffset (
                    stackFrames.at (static_cast<size_t> (i)).InstructionOffset,
                    &line,
                    fileBuffer,
                    kFileBufferSize,
                    &fileSize,
                    nullptr) };

                if (SUCCEEDED (lineResult))
                {
                    DynObj source { new juce::DynamicObject () };
                    source->setProperty ("name", juce::File (juce::String (fileBuffer)).getFileName ());
                    source->setProperty ("path", juce::String (fileBuffer).replace ("\\", "/"));
                    frame->setProperty ("source", juce::var (source));
                    frame->setProperty ("line", static_cast<int> (line));
                    frame->setProperty ("column", 1);
                }

                frames.add (juce::var (frame));
            }
        }
    }

    return frames;
}

juce::Array<juce::var> Session::getLocals (int frameIndex) noexcept
{
    juce::Array<juce::var> variables;

    if (symbols != nullptr)
    {
        const HRESULT scopeResult { symbols->SetScopeFrameByIndex (static_cast<ULONG> (frameIndex)) };

        if (SUCCEEDED (scopeResult))
        {
            IDebugSymbolGroup2* group { nullptr };
            const HRESULT groupResult { symbols->GetScopeSymbolGroup2 (
                DEBUG_SCOPE_GROUP_ALL, nullptr, &group) };

            if (SUCCEEDED (groupResult) and group != nullptr)
            {
                static constexpr int kSymbolNameSize  { 256 };
                static constexpr int kSymbolTypeSize  { 256 };
                static constexpr int kSymbolValueSize { 512 };

                ULONG count { 0 };
                group->GetNumberSymbols (&count);

                for (ULONG i { 0 }; i < count; ++i)
                {
                    DEBUG_SYMBOL_PARAMETERS params {};
                    const HRESULT paramResult { group->GetSymbolParameters (i, 1, &params) };

                    if (SUCCEEDED (paramResult) and params.ParentSymbol == DEBUG_ANY_ID)
                    {
                        char nameBuffer[kSymbolNameSize] {};
                        group->GetSymbolName (i, nameBuffer, kSymbolNameSize, nullptr);

                        const juce::String symbolName { nameBuffer };

                        if (not symbolName.startsWithChar ('<'))
                        {
                            char typeBuffer[kSymbolTypeSize] {};
                            group->GetSymbolTypeName (i, typeBuffer, kSymbolTypeSize, nullptr);

                            char valueBuffer[kSymbolValueSize] {};
                            const HRESULT valueResult { group->GetSymbolValueText (i, valueBuffer, kSymbolValueSize, nullptr) };

                            DynObj var { new juce::DynamicObject () };
                            juce::String displayValue { SUCCEEDED (valueResult) ? formatSymbolValue (juce::String (valueBuffer)) : juce::String ("<unavailable>") };

                            const juce::String typeName { typeBuffer };
                            const juce::String prettyValue { prettyPrint (symbols.Get (), dataSpaces.Get (), frameIndex, static_cast<int> (i), typeName) };

                            if (prettyValue.isNotEmpty ())
                            {
                                displayValue = prettyValue;
                            }

                            var->setProperty ("name",        symbolName);
                            var->setProperty ("value",       displayValue);
                            var->setProperty ("type",        typeName);
                            var->setProperty ("symbolIndex", static_cast<int> (i));
                            var->setProperty ("hasChildren", params.SubElements > 0);

                            variables.add (juce::var (var));
                        }
                    }
                }

                group->Release ();
            }

            symbols->ResetScope ();
        }
    }

    return variables;
}

juce::Array<juce::var> Session::getVariableChildren (int frameIndex, int symbolIndex) noexcept
{
    juce::Array<juce::var> variables;

    if (symbols != nullptr)
    {
        const HRESULT scopeResult { symbols->SetScopeFrameByIndex (static_cast<ULONG> (frameIndex)) };

        if (SUCCEEDED (scopeResult))
        {
            IDebugSymbolGroup2* group { nullptr };
            const HRESULT groupResult { symbols->GetScopeSymbolGroup2 (
                DEBUG_SCOPE_GROUP_ALL, nullptr, &group) };

            if (SUCCEEDED (groupResult) and group != nullptr)
            {
                static constexpr int kSymbolNameSize  { 256 };
                static constexpr int kSymbolTypeSize  { 256 };
                static constexpr int kSymbolValueSize { 512 };

                const ULONG parentIndex { static_cast<ULONG> (symbolIndex) };
                const HRESULT expandResult { group->ExpandSymbol (parentIndex, TRUE) };

                if (SUCCEEDED (expandResult))
                {
                    ULONG count { 0 };
                    group->GetNumberSymbols (&count);

                    for (ULONG i { 0 }; i < count; ++i)
                    {
                        DEBUG_SYMBOL_PARAMETERS params {};
                        const HRESULT paramResult { group->GetSymbolParameters (i, 1, &params) };

                        if (SUCCEEDED (paramResult) and params.ParentSymbol == parentIndex)
                        {
                            char nameBuffer[kSymbolNameSize] {};
                            group->GetSymbolName (i, nameBuffer, kSymbolNameSize, nullptr);

                            const juce::String symbolName { nameBuffer };

                            if (not symbolName.startsWithChar ('<'))
                            {
                                char typeBuffer[kSymbolTypeSize] {};
                                group->GetSymbolTypeName (i, typeBuffer, kSymbolTypeSize, nullptr);

                                char valueBuffer[kSymbolValueSize] {};
                                const HRESULT valueResult { group->GetSymbolValueText (i, valueBuffer, kSymbolValueSize, nullptr) };

                                DynObj var { new juce::DynamicObject () };
                                juce::String displayValue { SUCCEEDED (valueResult) ? formatSymbolValue (juce::String (valueBuffer)) : juce::String ("<unavailable>") };

                                const juce::String typeName { typeBuffer };
                                const juce::String prettyValue { prettyPrint (symbols.Get (), dataSpaces.Get (), frameIndex, static_cast<int> (i), typeName) };

                                if (prettyValue.isNotEmpty ())
                                {
                                    displayValue = prettyValue;
                                }

                                var->setProperty ("name",        symbolName);
                                var->setProperty ("value",       displayValue);
                                var->setProperty ("type",        typeName);
                                var->setProperty ("symbolIndex", static_cast<int> (i));
                                var->setProperty ("hasChildren", params.SubElements > 0);

                                variables.add (juce::var (var));
                            }
                        }
                    }
                }

                group->Release ();
            }

            symbols->ResetScope ();
        }
    }

    return variables;
}

} // namespace debug
