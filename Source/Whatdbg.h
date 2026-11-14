#pragma once
#include <JuceHeader.h>
#include "WHATDBGModel.h"
#include "Processor.h"
#include "dap/Writer.h"
#include "dap/Reader.h"

/** Application object of the adapter. Runs on the JUCE message thread.
 *
 *  Owns the model, the engine context (Processor), the stdout writer, and the stdin
 *  reader. The message thread writes stdout and makes no engine call. Processor owns
 *  the engine context. The reader thread calls Processor::addRequest through onMessage.
 *
 *  Members are destroyed in reverse declaration order, so the reader is destroyed
 *  before the processor.
 */
class Whatdbg : public juce::JUCEApplicationBase,
                private juce::ValueTree::Listener
{
public:
    /** Creates the model listener, the two parameter attachments, and the reader route. */
    Whatdbg ();

    /** Removes the model listener. */
    ~Whatdbg () override;

    /** Returns the project name from ProjectInfo.
     *
     *  @return The project name.
     */
    const juce::String getApplicationName () override;

    /** Returns the version string from ProjectInfo.
     *
     *  @return The version string.
     */
    const juce::String getApplicationVersion () override;

    /** Allows more than one instance. Each process runs its own adapter.
     *
     *  @return true.
     */
    bool moreThanOneInstanceAllowed () override;

    /** Prepares the engine context with the sidecar folder and starts the reader.
     *
     *  The sidecar folder is the first command-line parameter that Main.cpp passes to
     *  JUCEApplicationBase::main. The reader starts only when the isRunning parameter is
     *  not zero.
     *
     *  @param commandLine  Command-line text that JUCE passes. Not used.
     */
    void initialise (const juce::String& commandLine) override;

    /** Does nothing. The member destructors release the session. */
    void shutdown () override;

    /** Does nothing.
     *
     *  @param commandLine  Command-line text of the second instance. Not used.
     */
    void anotherInstanceStarted (const juce::String& commandLine) override;

    /** Quits the application. */
    void systemRequestedQuit () override;

    /** Does nothing. */
    void suspended () override;

    /** Does nothing. */
    void resumed () override;

    /** Runs jassertfalse. An unhandled exception is not expected.
     *
     *  @param exception       Exception that reached the message loop. Not used.
     *  @param sourceFilename  Source file that reported the exception. Not used.
     *  @param lineNumber      Line that reported the exception. Not used.
     */
    void unhandledException (const std::exception* exception, const juce::String& sourceFilename, int lineNumber) override;

private:
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;

    void registerEvents ();

    WHATDBGModel model;

    Processor processor;

    dap::Writer writer;

    dap::Reader reader;

    jam::Function::Map<juce::Identifier, jam::Function::pointerCapacity, void (const juce::ValueTree&)> events;

    std::unique_ptr<jam::Model::ParameterAttachment> numMessagesAttachment;

    std::unique_ptr<jam::Model::ParameterAttachment> isRunningAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Whatdbg)
};
