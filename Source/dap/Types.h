#pragma once
#include <JuceHeader.h>
#include "../generated/Generated.h"

namespace dap
{

/** Return the next DAP sequence number and store the value after it.
 *
 *  Responses, events, and requests share one sequence. The stored value starts at 1.
 *
 *  @return The sequence number of the message being built.
 */
int nextSeq () noexcept;

/** Append the members of a response object: seq, type, request_seq, command, and success.
 *
 *  The caller opens and closes the object.
 *
 *  @param json        Output text that receives the members.
 *  @param requestSeq  Sequence number of the request that this response answers.
 *  @param command     DAP command name.
 *  @param isSuccess   Value of the success member.
 */
void appendResponseMembers (std::string& json, int requestSeq, std::string_view command, bool isSuccess);

/** Append the members of an event object: seq, type, and event.
 *
 *  The caller opens and closes the object.
 *
 *  @param json   Output text that receives the members.
 *  @param event  DAP event name.
 */
void appendEventMembers (std::string& json, std::string_view event);

/** Append a response object that has a body.
 *
 *  @tparam Function  Callable that takes a std::string& and appends the body members.
 *  @param json        Output text that receives the response object.
 *  @param requestSeq  Sequence number of the request that this response answers.
 *  @param command     DAP command name.
 *  @param isSuccess   Value of the success member.
 *  @param appendBody  Appends the members of the body object.
 */
template <typename Function>
void appendResponse (std::string& json, int requestSeq, std::string_view command, bool isSuccess, const Function& appendBody)
{
    jam::JsonWriter::appendObject (json, [requestSeq, command, isSuccess, &appendBody] (std::string& members)
    {
        appendResponseMembers (members, requestSeq, command, isSuccess);
        jam::JsonWriter::appendKey (members, Id::body);
        jam::JsonWriter::appendObject (members, appendBody);
    });
}

/** Append a response object without a body.
 *
 *  @param json        Output text that receives the response object.
 *  @param requestSeq  Sequence number of the request that this response answers.
 *  @param command     DAP command name.
 *  @param isSuccess   Value of the success member.
 */
void appendResponse (std::string& json, int requestSeq, std::string_view command, bool isSuccess);

/** Append a failed response object with a message member.
 *
 *  @param json        Output text that receives the response object.
 *  @param requestSeq  Sequence number of the request that this response answers.
 *  @param command     DAP command name.
 *  @param message     Failure text, written to the message member.
 */
void appendErrorResponse (std::string& json, int requestSeq, std::string_view command, std::string_view message);

/** Append an event object that has a body.
 *
 *  @tparam Function  Callable that takes a std::string& and appends the body members.
 *  @param json        Output text that receives the event object.
 *  @param event       DAP event name.
 *  @param appendBody  Appends the members of the body object.
 */
template <typename Function>
void appendEvent (std::string& json, std::string_view event, const Function& appendBody)
{
    jam::JsonWriter::appendObject (json, [event, &appendBody] (std::string& members)
    {
        appendEventMembers (members, event);
        jam::JsonWriter::appendKey (members, Id::body);
        jam::JsonWriter::appendObject (members, appendBody);
    });
}

/** Append an event object without a body.
 *
 *  @param json   Output text that receives the event object.
 *  @param event  DAP event name.
 */
void appendEvent (std::string& json, std::string_view event);

/** Return a request document with a command and an arguments object.
 *
 *  @tparam Function       Callable that takes a std::string& and appends the argument members.
 *  @param command         DAP command name.
 *  @param appendArguments Appends the members of the arguments object.
 *  @return The parsed request document.
 */
template <typename Function>
jam::JsonDocument getRequest (std::string_view command, const Function& appendArguments)
{
    std::string json;

    jam::JsonWriter::appendObject (json, [command, &appendArguments] (std::string& members)
    {
        jam::JsonWriter::appendKey (members, Id::seq);
        jam::Format::appendDecimal (members, nextSeq ());
        jam::JsonWriter::appendKey (members, Id::type);
        jam::JsonWriter::appendString (members, jam::Strings::getView (Id::request.toString ()));
        jam::JsonWriter::appendKey (members, Id::command);
        jam::JsonWriter::appendString (members, command);
        jam::JsonWriter::appendKey (members, Id::arguments);
        jam::JsonWriter::appendObject (members, appendArguments);
    });

    return std::move (*jam::JsonDocument::parse (std::move (json)));
}

/** Return a request document with a command and no arguments member.
 *
 *  @param command  DAP command name.
 *  @return The parsed request document.
 */
jam::JsonDocument getRequest (std::string_view command);

/** Append a breakpoint changed event that reports a verified breakpoint at a line.
 *
 *  @param json   Output text that receives the event object.
 *  @param dapId  DAP breakpoint id.
 *  @param line   Resolved source line.
 */
void appendBreakpointChangedEvent (std::string& json, int dapId, int line);

/** Append a source object with a name and a path.
 *
 *  @param json  Output text that receives the source object.
 *  @param name  File name.
 *  @param path  Full path of the file.
 */
void appendSource (std::string& json, std::string_view name, std::string_view path);

/** Append the capability flags of the initialize response.
 *
 *  The flags are key and value pairs. The caller opens and closes the object.
 *
 *  @param json  Output text that receives the flags.
 */
void appendCapabilities (std::string& json);

/** Return a stopped event for a stop reason on one thread, with allThreadsStopped set to true.
 *
 *  @param reason    DAP stop reason, for example step or pause.
 *  @param threadId  OS thread id of the thread that stopped.
 *  @return The serialized event.
 */
std::string getStoppedEvent (std::string_view reason, int threadId);

/** Return the text of an exception: its code and its address, in hexadecimal.
 *
 *  @param exceptionCode     Platform exception or signal code.
 *  @param exceptionAddress  Address where the exception occurred.
 *  @return Text in the form 0x\<code\> at 0x\<address\>, in hexadecimal digits.
 */
juce::String getExceptionDescription (juce::int64 exceptionCode, juce::int64 exceptionAddress);

/** Return a stopped event with reason exception.
 *
 *  @param exceptionName  Name from debug::getExceptionName.
 *  @param description    Text from getExceptionDescription.
 *  @param threadId       OS thread id of the thread that stopped.
 *  @return The serialized event.
 */
std::string getExceptionStoppedEvent (const juce::String& exceptionName, const juce::String& description, int threadId);

/** Return an output event on standard error that reports an unhandled exception.
 *
 *  @param exceptionName  Name from debug::getExceptionName.
 *  @param description    Text from getExceptionDescription.
 *  @return The serialized event.
 */
std::string getExceptionOutputEvent (const juce::String& exceptionName, const juce::String& description);

/** Return a process event for the target process.
 *
 *  @param name         Program name or attach label.
 *  @param processId    OS process id.
 *  @param startMethod  DAP start method, launch or attach.
 *  @return The serialized event.
 */
std::string getProcessEvent (const juce::String& name, std::uint32_t processId, std::string_view startMethod);

/** Return an output event with a category and a text.
 *
 *  @param category  DAP output category. Processor passes the name of the Id that fired the stream event.
 *  @param output    Output text.
 *  @return The serialized event.
 */
std::string getOutputEvent (std::string_view category, std::string_view output);

} // namespace dap
