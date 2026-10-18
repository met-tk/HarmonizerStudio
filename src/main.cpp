#ifdef _WIN32
#include <windows.h>
#endif
#include <juce_gui_basics/juce_gui_basics.h>
#include "ui/MainComponent.h"
#include "ui/CubaseTheme.h"
#include "core/AppStateManager.h"
#include "core/UndoSelfTests.h"
#include "core/ProjectSelfTests.h"
#include "core/SecondaryDominantSelfTests.h"
#include "core/ReharmonizerSelfTests.h"
#include "core/ChordEditingSelfTests.h"
#include "core/QuickChordSelfTests.h"
#include "core/ModeAnalyzerSelfTests.h"
#include "core/ChordTheorySelfTests.h"
#include "audio/ChannelStripMathSelfTests.h"

namespace Harmonizer
{

class MainWindow : public juce::DocumentWindow,
                   private juce::ComponentListener
{
public:
    MainWindow(juce::String name)
        : DocumentWindow(name,
                         CubaseTheme::bgDeep(),
                         DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);

        auto* mc = new MainComponent();
        mainComp = mc;
        setContentOwned(mc, true);

        setResizable(true, true);
        setResizeLimits(1024, 500, 3840, 2160);

        // Restore window geometry from AppStateManager
        auto& st = AppStateManager::getInstance();
        if (st.mainWindowMaximized)
        {
            centreWithSize(st.mainWindowW, st.mainWindowH);
            setFullScreen(true);
        }
        else
        {
            setBounds(st.mainWindowX, st.mainWindowY, st.mainWindowW, st.mainWindowH);
            // Keep window on screen
            auto screens = juce::Desktop::getInstance().getDisplays().getTotalBounds(true);
            if (!screens.intersects(getBounds().reduced(50)))
                centreWithSize(st.mainWindowW, st.mainWindowH);
        }

        addComponentListener(this);
        setVisible(true);
    }

    ~MainWindow() override
    {
        removeComponentListener(this);
    }

    void closeButtonPressed() override
    {
        // Save window geometry before quitting
        saveWindowState();
        if (mainComp)
            mainComp->collectAndSaveUIState();
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

    void saveWindowState()
    {
        auto& st = AppStateManager::getInstance();
        st.mainWindowMaximized = isFullScreen();
        if (!isFullScreen())
        {
            st.mainWindowX = getX();
            st.mainWindowY = getY();
            st.mainWindowW = getWidth();
            st.mainWindowH = getHeight();
        }
    }

private:
    MainComponent* mainComp = nullptr;

    // ComponentListener — save on every move/resize so crash doesn't lose geometry
    void componentMovedOrResized(juce::Component&, bool, bool) override
    {
        saveWindowState();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

class HarmonizerStudioApp : public juce::JUCEApplication
{
public:
    HarmonizerStudioApp() = default;

    const juce::String getApplicationName() override       { return "Harmonizer Studio"; }
    const juce::String getApplicationVersion() override    { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override
    {
        // The self-test modes exit without creating a window and must remain
        // runnable while the editor is open.
        return isSelfTestCommandLine(getCommandLineParameters());
    }

    void initialise(const juce::String& commandLine) override
    {
        if (isSelfTestCommandLine(commandLine))
        {
#if JUCE_WINDOWS
            AttachConsole(ATTACH_PARENT_PROCESS);
            FILE* fDummy = nullptr;
            freopen_s(&fDummy, "CONOUT$", "w", stdout);
            freopen_s(&fDummy, "CONOUT$", "w", stderr);
#endif
            int code = commandLine.contains("--run-chord-theory-tests") ? runChordTheorySelfTests()
                : commandLine.contains("--run-channel-strip-tests") ? runChannelStripMathSelfTests()
                : commandLine.contains("--run-theme-preset-tests") ? runThemePresetSelfTests()
                : commandLine.contains("--run-mode-colour-tests") ? runModeAnalyzerSelfTests()
                : commandLine.contains("--run-reharmonization-tests") ? runReharmonizerSelfTests()
                : commandLine.contains("--run-chord-editing-tests") ? runChordEditingSelfTests()
                : commandLine.contains("--run-quick-chord-tests") ? runQuickChordSelfTests()
                : commandLine.contains("--run-harmony-tests") ? runSecondaryDominantSelfTests()
                : commandLine.contains("--run-project-tests") ? runProjectSelfTests()
                : runUndoSystemSelfTests();
            setApplicationReturnValue(code);
            quit();
            return;
        }

        // Load persisted state before creating any UI
        AppStateManager::getInstance().load();

        theme.updateLookAndFeelColours();
        juce::LookAndFeel::setDefaultLookAndFeel(&theme);
        mainWindow = std::make_unique<MainWindow>(getApplicationName() + " - Cubase Edition");
    }

    void shutdown() override
    {
        if (mainWindow != nullptr)
        {
            mainWindow->saveWindowState();
            if (auto* mc = dynamic_cast<MainComponent*>(mainWindow->getContentComponent()))
                mc->collectAndSaveUIState();
        }
        mainWindow.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String&) override
    {
        if (mainWindow != nullptr)
        {
            mainWindow->setVisible(true);
            mainWindow->toFront(true);
        }
    }

private:
    static bool isSelfTestCommandLine(const juce::String& commandLine)
    {
        return commandLine.contains("--run-undo-tests")
            || commandLine.contains("--run-channel-strip-tests")
            || commandLine.contains("--run-chord-theory-tests")
            || commandLine.contains("--run-theme-preset-tests")
            || commandLine.contains("--run-project-tests")
            || commandLine.contains("--run-harmony-tests")
            || commandLine.contains("--run-reharmonization-tests")
            || commandLine.contains("--run-chord-editing-tests")
            || commandLine.contains("--run-quick-chord-tests")
            || commandLine.contains("--run-mode-colour-tests");
    }

    CubaseTheme theme;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace Harmonizer

START_JUCE_APPLICATION(Harmonizer::HarmonizerStudioApp)
