#pragma once
#include <JuceHeader.h>
#include "WHATDBGModel.h"
#include "Processor.h"

class Whatdbg : public juce::JUCEApplicationBase,
                private juce::ValueTree::Listener
{
public:
    Whatdbg ();

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
    /** Calls the handler that registerEvents added for the changed property, when one exists.
     *
     *  @param tree      The value tree of the changed property. The handler receives it.
     *  @param property  The changed property. Its identifier selects the handler.
     */
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;

    void registerEvents ();

    /** The model. Its parameters hold the debug state, and the attachments and the handlers read them. */
    WHATDBGModel model;

    Processor processor;

    /** The handlers of model property changes, keyed by property identifier. */
    jam::Function::Map<juce::Identifier, jam::Function::pointerCapacity, void (const juce::ValueTree&)> events;

    /** Calls model.flush when the isRunning parameter changes. */
    std::unique_ptr<jam::Model::ParameterAttachment> isRunningAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Whatdbg)
};
