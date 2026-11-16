#pragma once
#include <JuceHeader.h>

/** Runs in the terminal that the client opens for a launch.
 *
 *  Both platforms create the target suspended, send the target's process ID, and read the adapter's reply.
 *  The target runs only after the debugger resumes it.
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
     *  macOS opens the reply FIFO with close-on-exec.
     *  It spawns the target with POSIX_SPAWN_START_SUSPENDED.
     *  It watches the exit of the target with a kqueue.
     *  It sends the process ID of the target.
     *  When the reply says didAttach, it waits for the exit event and returns the exit code of the target.
     *  A target that a signal killed gives 128 plus the signal number.
     *  Otherwise it kills the target with SIGKILL.
     *  When the spawn fails, it sends an error message.
     *
     *  @return The exit code of the target on both platforms. failedLaunchExitCode when the target does not
     *          start or does not attach.
     */
    int run ();

private:
    /** Writes the process ID message: a JSON object with kind pid and the process ID, on one line.
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
     *  macOS passes them to posix_spawnp.
     */
    juce::StringArray launchTarget;

#if JUCE_WINDOWS
    /** Handle of the channel pipe that run opens. Closed when the member is destroyed. Windows only. */
    std::unique_ptr<void, decltype (&CloseHandle)> pipe { nullptr, &CloseHandle };
#endif

#if JUCE_MAC
    /** Stream of the reply FIFO that run opens with close-on-exec, so the target does not inherit it.
     *  Closed when the member is destroyed. macOS only.
     */
    std::unique_ptr<FILE, decltype (&fclose)> readFifo { nullptr, &fclose };
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Launcher)
};
