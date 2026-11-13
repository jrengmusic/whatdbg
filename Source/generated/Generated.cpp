/*******************************************************************************
                        Codegen Annotated Source of Truth
————————————————————————————————————————————————————————————————————————————————

            ░░████████████░░████████████░░████████████░░████████████
            ░░████  ░░████░░████  ░░████░░████  ░░████    ░░████
            ░░████        ░░████  ░░████░░████            ░░████
            ░░████        ░░████████████░░████████████    ░░████
            ░░████        ░░████  ░░████        ░░████    ░░████
            ░░████  ░░████░░████  ░░████░░████  ░░████    ░░████
            ░░████████████░░████  ░░████░░████████████    ░░████

————————————————————————————————————————————————————————————————————————————————
                         FOR YOUR EYES ONLY, DO NOT EDIT
********************************************************************************/

#include <JuceHeader.h>
#include "Generated.h"

namespace Id
{
/*_____________________________________________________________________________*/

/** @brief DAP vocabulary — the keys and the fixed words of the debug adapter protocol messages. */

    const juce::Identifier allThreadsContinued { juce::String::fromUTF8 ("allThreadsContinued") };
    const juce::Identifier allThreadsStopped   { juce::String::fromUTF8 ("allThreadsStopped") };
    const juce::Identifier arguments           { juce::String::fromUTF8 ("arguments") };
    const juce::Identifier attach              { juce::String::fromUTF8 ("attach") };
    const juce::Identifier body                { juce::String::fromUTF8 ("body") };
    const juce::Identifier breakMode           { juce::String::fromUTF8 ("breakMode") };
    const juce::Identifier breakpoint          { juce::String::fromUTF8 ("breakpoint") };
    const juce::Identifier breakpoints         { juce::String::fromUTF8 ("breakpoints") };
    const juce::Identifier category            { juce::String::fromUTF8 ("category") };
    const juce::Identifier changed             { juce::String::fromUTF8 ("changed") };
    const juce::Identifier column              { juce::String::fromUTF8 ("column") };
    const juce::Identifier command             { juce::String::fromUTF8 ("command") };
    const juce::Identifier configurationDone   { juce::String::fromUTF8 ("configurationDone") };
    const juce::Identifier console             { juce::String::fromUTF8 ("console") };
    const juce::Identifier cwd                 { juce::String::fromUTF8 ("cwd") };
    const juce::Identifier disconnect          { juce::String::fromUTF8 ("disconnect") };
    const juce::Identifier evaluate            { juce::String::fromUTF8 ("evaluate") };
    const juce::Identifier event               { juce::String::fromUTF8 ("event") };
    const juce::Identifier exception           { juce::String::fromUTF8 ("exception") };
    const juce::Identifier exceptionId         { juce::String::fromUTF8 ("exceptionId") };
    const juce::Identifier exceptionInfo       { juce::String::fromUTF8 ("exceptionInfo") };
    const juce::Identifier exitCode            { juce::String::fromUTF8 ("exitCode") };
    const juce::Identifier exited              { juce::String::fromUTF8 ("exited") };
    const juce::Identifier expensive           { juce::String::fromUTF8 ("expensive") };
    const juce::Identifier expression          { juce::String::fromUTF8 ("expression") };
    const juce::Identifier frameId             { juce::String::fromUTF8 ("frameId") };
    const juce::Identifier hitBreakpointIds    { juce::String::fromUTF8 ("hitBreakpointIds") };
    const juce::Identifier initialize          { juce::String::fromUTF8 ("initialize") };
    const juce::Identifier initialized         { juce::String::fromUTF8 ("initialized") };
    const juce::Identifier isLocalProcess      { juce::String::fromUTF8 ("isLocalProcess") };
    const juce::Identifier launch              { juce::String::fromUTF8 ("launch") };
    const juce::Identifier locals              { juce::String::fromUTF8 ("Locals") };
    const juce::Identifier message             { juce::String::fromUTF8 ("message") };
    const juce::Identifier next                { juce::String::fromUTF8 ("next") };
    const juce::Identifier output              { juce::String::fromUTF8 ("output") };
    const juce::Identifier pause               { juce::String::fromUTF8 ("pause") };
    const juce::Identifier pid                 { juce::String::fromUTF8 ("pid") };
    const juce::Identifier process             { juce::String::fromUTF8 ("process") };
    const juce::Identifier program             { juce::String::fromUTF8 ("program") };
    const juce::Identifier reason              { juce::String::fromUTF8 ("reason") };
    const juce::Identifier request             { juce::String::fromUTF8 ("request") };
    const juce::Identifier requestSeq          { juce::String::fromUTF8 ("request_seq") };
    const juce::Identifier response            { juce::String::fromUTF8 ("response") };
    const juce::Identifier result              { juce::String::fromUTF8 ("result") };
    const juce::Identifier scopes              { juce::String::fromUTF8 ("scopes") };
    const juce::Identifier seq                 { juce::String::fromUTF8 ("seq") };
    const juce::Identifier setBreakpoints      { juce::String::fromUTF8 ("setBreakpoints") };
    const juce::Identifier source              { juce::String::fromUTF8 ("source") };
    const juce::Identifier stackFrames         { juce::String::fromUTF8 ("stackFrames") };
    const juce::Identifier stackTrace          { juce::String::fromUTF8 ("stackTrace") };
    const juce::Identifier started             { juce::String::fromUTF8 ("started") };
    const juce::Identifier startMethod         { juce::String::fromUTF8 ("startMethod") };
    const juce::Identifier step                { juce::String::fromUTF8 ("step") };
    const juce::Identifier stepIn              { juce::String::fromUTF8 ("stepIn") };
    const juce::Identifier stepOut             { juce::String::fromUTF8 ("stepOut") };
    const juce::Identifier stopped             { juce::String::fromUTF8 ("stopped") };
    const juce::Identifier success             { juce::String::fromUTF8 ("success") };
    const juce::Identifier systemProcessId     { juce::String::fromUTF8 ("systemProcessId") };
    const juce::Identifier terminate           { juce::String::fromUTF8 ("terminate") };
    const juce::Identifier terminated          { juce::String::fromUTF8 ("terminated") };
    const juce::Identifier terminateDebuggee   { juce::String::fromUTF8 ("terminateDebuggee") };
    const juce::Identifier thread              { juce::String::fromUTF8 ("thread") };
    const juce::Identifier threadId            { juce::String::fromUTF8 ("threadId") };
    const juce::Identifier threads             { juce::String::fromUTF8 ("threads") };
    const juce::Identifier tokenContinue       { juce::String::fromUTF8 ("continue") };
    const juce::Identifier tokenStderr         { juce::String::fromUTF8 ("stderr") };
    const juce::Identifier tokenStdout         { juce::String::fromUTF8 ("stdout") };
    const juce::Identifier totalFrames         { juce::String::fromUTF8 ("totalFrames") };
    const juce::Identifier unhandled           { juce::String::fromUTF8 ("unhandled") };
    const juce::Identifier variables           { juce::String::fromUTF8 ("variables") };
    const juce::Identifier variablesReference  { juce::String::fromUTF8 ("variablesReference") };
    const juce::Identifier verified            { juce::String::fromUTF8 ("verified") };

/**______________________________END OF NAMESPACE______________________________*/
}// namespace Id
