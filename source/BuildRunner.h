#pragma once

// Runs a sequence of shell commands (CMake configure + build) on a background
// thread, streaming their output to the message thread line by line. One runner
// per build; kill() aborts.

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <functional>

namespace dmse_studio
{

class BuildRunner : private juce::Thread
{
public:
    std::function<void (const juce::String&)> onOutput;   // message thread
    std::function<void (bool success)> onFinished;        // message thread

    BuildRunner() : juce::Thread ("dmse build") {}
    ~BuildRunner() override { stop(); }

    /** Start the given commands (each a full command line), run in order; stops at
        the first failure. */
    void start (juce::StringArray commandLines, const juce::File& workingDir)
    {
        stop();
        commands = std::move (commandLines);
        cwd = workingDir;
        startThread();
    }

    bool isRunning() const { return isThreadRunning(); }

    void stop()
    {
        process.kill();
        stopThread (5000);
    }

private:
    void run() override
    {
        bool ok = true;
        for (const auto& cmd : commands)
        {
            if (threadShouldExit())
                { ok = false; break; }

            post ("$ " + cmd + "\n");

            // ChildProcess has no working-directory support — wrap in a shell cd.
           #if JUCE_WINDOWS
            const auto full = "cmd /c cd /d \"" + cwd.getFullPathName() + "\" && " + cmd;
           #else
            const auto full = "/bin/sh -c \"cd '" + cwd.getFullPathName() + "' && " + cmd + "\"";
           #endif
            if (! process.start (full))
            {
                post ("could not start: " + cmd + "\n");
                ok = false;
                break;
            }

            char buffer[4096];
            for (;;)
            {
                const int n = process.readProcessOutput (buffer, sizeof (buffer));
                if (n <= 0)
                    break;
                post (juce::String::fromUTF8 (buffer, n));
                if (threadShouldExit())
                    process.kill();
            }
            const auto exitCode = process.getExitCode();
            if (exitCode != 0)
            {
                post ("\ncommand failed (exit " + juce::String ((int) exitCode) + ")\n");
                ok = false;
                break;
            }
        }

        const bool success = ok && ! threadShouldExit();
        juce::MessageManager::callAsync ([this, success]
        {
            if (onFinished)
                onFinished (success);
        });
    }

    void post (const juce::String& text)
    {
        juce::MessageManager::callAsync ([this, text]
        {
            if (onOutput)
                onOutput (text);
        });
    }

    juce::StringArray commands;
    juce::File cwd;
    juce::ChildProcess process;
};

} // namespace dmse_studio
