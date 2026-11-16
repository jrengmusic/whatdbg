#include "Whatdbg.h"

Whatdbg::Whatdbg ()
{
    registerEvents ();
    model.addListener (this);

    isRunningAttachment = std::make_unique<jam::Model::ParameterAttachment> (*model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::isRunning),
                                                                            [this] (const juce::var&)
                                                                            {
                                                                                model.flush ();
                                                                            });
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
    events.add (Id::isRunning, [this] (const juce::Identifier&, const juce::ValueTree& tree)
    {
        if (static_cast<int> (tree.getProperty (Id::isRunning)) == 0)
        {
            setApplicationReturnValue (model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::adapterExitCode)->getValue ());
            quit ();
        }
    });
}
