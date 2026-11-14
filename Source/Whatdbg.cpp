/** @file Whatdbg.cpp
 *  @brief Application lifecycle, the route from the reader to the processor, and the two model event handlers.
 *
 *  The message thread runs the constructor, initialise, and the model listener. The
 *  reader callback, which Whatdbg sets in its constructor, runs on the reader thread.
 *  The numMessages change flushes queued messages to the writer. The isRunning change
 *  to zero quits the application.
 */

#include "Whatdbg.h"

Whatdbg::Whatdbg ()
{
    registerEvents ();
    model.addListener (this);

    numMessagesAttachment = std::make_unique<jam::Model::ParameterAttachment> (*model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::numMessages),
                                                                              [this] (const juce::var&)
                                                                              {
                                                                                  model.flush ();
                                                                              });

    isRunningAttachment = std::make_unique<jam::Model::ParameterAttachment> (*model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::isRunning),
                                                                            [this] (const juce::var&)
                                                                            {
                                                                                model.flush ();
                                                                            });

    reader.onMessage = [this] (jam::JsonDocument message)
    {
        processor.addRequest (std::move (message));
    };
}

Whatdbg::~Whatdbg ()
{
    model.removeListener (this);
}

const juce::String Whatdbg::getApplicationName ()
{
    return ProjectInfo::projectName;
}

const juce::String Whatdbg::getApplicationVersion ()
{
    return ProjectInfo::versionString;
}

bool Whatdbg::moreThanOneInstanceAllowed ()
{
    return true;
}

void Whatdbg::initialise (const juce::String&)
{
    processor.prepare (juce::File { getCommandLineParameterArray ()[0] });

    if (model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::isRunning)->getValue () != 0)
    {
        reader.start ();
    }
}

void Whatdbg::shutdown ()
{
}

void Whatdbg::anotherInstanceStarted (const juce::String&)
{
}

void Whatdbg::systemRequestedQuit ()
{
    quit ();
}

void Whatdbg::suspended ()
{
}

void Whatdbg::resumed ()
{
}

void Whatdbg::unhandledException (const std::exception*, const juce::String&, int)
{
    jassertfalse;
}

void Whatdbg::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (events.contains (property))
        events.get (property, tree);
}

void Whatdbg::registerEvents ()
{
    events.add (Id::numMessages, [this] (const juce::Identifier&, const juce::ValueTree&)
    {
        processor.applyMessages ([this] (const std::string& message) { writer.writeMessage (message); });
    });

    events.add (Id::isRunning, [this] (const juce::Identifier&, const juce::ValueTree& tree)
    {
        if (static_cast<int> (tree.getProperty (Id::isRunning)) == 0)
        {
            setApplicationReturnValue (model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::adapterExitCode)->getValue ());
            quit ();
        }
    });
}
