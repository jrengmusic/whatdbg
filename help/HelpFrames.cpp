#include <JuceHeader.h>

static bool writeFrame (const juce::File& frameFile, const juce::MemoryOutputStream& frame)
{
    frameFile.deleteFile();
    juce::FileOutputStream stream { frameFile };

    if (stream.openedOk() and stream.write (frame.getData(), frame.getDataSize()))
    {
        stream.flush();
        return stream.getStatus().wasOk();
    }

    return false;
}

static bool writeHelpFrame (const juce::File& frameFile, const juce::String& documentName)
{
    jam::TerminalPage page { { BinaryData::fetcher }, jam::Strings { files::styleSheet } };
    auto document { jam::MarkdownDocument::parse (BinaryData::getString (documentName), documentName) };
    assert (document != nullptr);
    page.setDocument (std::move (*document));
    juce::MemoryOutputStream frame;
    page.writeToStream (frame);
    return writeFrame (frameFile, frame);
}

static bool writeEraseFrame (const juce::File& frameFile)
{
    juce::MemoryOutputStream frame;
    jam::TerminalEncoder::writeEraseScreen (frame);
    return writeFrame (frameFile, frame);
}

int main (int argc, char* argv[])
{
    assert (argc > 3);

    const juce::ScopedJuceInitialiser_GUI juceInitialiser;
    const juce::File folder { juce::String::fromUTF8 (argv[1]) };
    const juce::File eraseFile { folder.getChildFile (juce::String::fromUTF8 (argv[2])) };
    bool isWritten { writeEraseFrame (eraseFile) };

    for (int index { 3 }; index < argc; ++index)
    {
        const auto documentName { juce::String::fromUTF8 (argv[index]) };
        const auto frameFile { folder.getChildFile (documentName).withFileExtension (".frame") };
        isWritten = writeHelpFrame (frameFile, documentName) and isWritten;
    }

    return isWritten ? EXIT_SUCCESS : EXIT_FAILURE;
}
