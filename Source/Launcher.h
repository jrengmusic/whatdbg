#pragma once
#include <JuceHeader.h>

/** Runs in the terminal that the client opens for a launch.
 *
 *  Sends the process ID of the target through the launch channel, then reads the reply of the
 *  adapter. It runs the target only when the reply says that the adapter attached.
 */
class Launcher
{
public:
    /** Stores the channel name and the target command line.
     *
     *  On Windows, an element that contains a space and does not start with a quote is quoted.
     *
     *  @param newCommFile      Channel name from the adapter: a pipe name on Windows, a FIFO path on macOS.
     *  @param newLaunchTarget  Target program and its arguments, one element for each.
     */
    Launcher (const juce::String& newCommFile, const juce::StringArray& newLaunchTarget);

    /** Starts the target and returns its exit code.
     *
     *  Windows creates the target suspended, sends its process ID, and reads the reply. When the
     *  reply says didAttach, it waits for the target to exit. Otherwise it terminates the target.
     *  When the target cannot be created, it sends an error message.
     *  macOS opens the reply FIFO, sends the process ID of this process, and reads the reply.
     *  When the reply says didAttach, it closes the reply FIFO and replaces this process with the
     *  target. When execvp fails, it prints the error.
     *
     *  @return The exit code of the target on Windows. failedLaunchExitCode when the target does not
     *          start or does not attach, and when execvp fails on macOS.
     */
    int run ();

private:
    /** Writes the process ID message: a JSON object with kind pid and the process ID, on one line.
     *
     *  On macOS the target replaces this process, so the ID names the target after execvp.
     *
     *  @param processId  Process ID of the target.
     */
    void sendPid (std::uint32_t processId);

    /** Writes the error message: a JSON object with kind error and the text, on one line.
     *
     *  @param text  Failure text that the adapter shows in the failed launch response.
     */
    void sendError (const juce::String& text);

    /** Writes one line to the channel. Windows writes to the pipe. macOS opens the FIFO for writing,
     *  writes the line, and closes the FIFO.
     *
     *  @param line  Bytes to write. The callers include the trailing newline.
     */
    void writeLine (const std::string& line);

    /** Reads one line from the channel and returns it without the newline.
     *
     *  Reads one byte at a time. On macOS, when fgetc fails with EINTR, the function clears the error and reads again.
     *  On macOS, reading stops at a newline, at the end of the channel, or at another read error.
     *  On Windows, reading stops at a newline or at a failed read.
     *
     *  @return The line text. Empty when the channel ends before any byte arrives.
     */
    std::string getLine ();

    /** Tells whether a reply line is the didAttach message.
     *
     *  @param line  Reply line from the adapter.
     *  @return true when the kind member of the JSON object is didAttach. false when the line
     *          does not parse as JSON, or when kind has another value.
     */
    static bool isAttached (const std::string& line);

    /** Exit code that run returns when the target does not start or does not attach. */
    static constexpr int failedLaunchExitCode { 1 };

    /** Channel name from the adapter. Windows opens it as a pipe. macOS writes to it, and reads the
     *  reply from the FIFO with the didAttach extension.
     */
    juce::String commFile;

    /** Target program and its arguments. Windows joins them with spaces into one command line.
     *  macOS passes them to execvp.
     */
    juce::StringArray launchTarget;

#if JUCE_WINDOWS
    /** Handle of the channel pipe that run opens. Closed when the member is destroyed. Windows only. */
    std::unique_ptr<void, decltype (&CloseHandle)> pipe { nullptr, &CloseHandle };
#endif

#if JUCE_MAC
    /** Stream of the reply FIFO that run opens. run closes it before execvp. macOS only. */
    std::unique_ptr<FILE, decltype (&fclose)> readFifo { nullptr, &fclose };
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Launcher)
};
