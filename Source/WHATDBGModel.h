#pragma once
#include <JuceHeader.h>
#include "generated/Generated.h"

/** Model of the adapter: one value tree with a session row and an exception row.
 *
 *  Each parameter is a value of the debug state. The engine context writes most
 *  parameters with storeValue. The numMessages and isRunning parameters use setValue,
 *  so that the message thread receives them at once.
 */
class WHATDBGModel
    : public jam::Model
    , public jam::Instance<WHATDBGModel>
{
public:
    /** Creates the session row and the exception row, and their parameters, at their default values. */
    WHATDBGModel ();

    /** Type of the session row, from Id::session. */
    const juce::Identifier sessionTag { jam::Format::toType (Id::session) };

    /** Type of the exception row, from Id::exception. */
    const juce::Identifier exceptionTag { jam::Format::toType (Id::exception) };

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WHATDBGModel)
};
