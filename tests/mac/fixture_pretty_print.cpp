#include <juce_core/juce_core.h>
#include <memory>
#include <string>
#include <vector>

int main (int, char**)
{
    const juce::String       juceStr { "hello" };
    const std::string        stdStr  { "hello" };
    const std::unique_ptr<int> uniq  { std::make_unique<int> (42) };
    const std::vector<int>   vec     { 1, 2, 3 };

    juce::ignoreUnused (juceStr, stdStr, uniq, vec);

    __builtin_debugtrap ();

    return 0;
}
