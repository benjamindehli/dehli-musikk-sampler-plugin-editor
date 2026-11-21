// dmse-screenshots: renders an instrument's Screenshots/ images from the plugin
// version, so they need not be made by hand each time the interface changes.
//
//   dmse-screenshots <plugin-repo> [--out <folder>] [--only <file>]
//
// The recipe is <plugin-repo>/Screenshots/screenshots.json:
//
//   {
//     "scale": 2,
//     "shots": [
//       { "file": "sub-c.png", "mode": "SubC", "window": true },
//       { "file": "envelope.png", "mode": "SubC",
//         "crop": { "x": 213, "y": 74, "width": 480, "height": 156 },
//         "set": { "Attack": 0.2, "btn_0": 1, "menu_0": 2 } }
//     ]
//   }
//
// "window" renders the whole standalone window, title bar included. "crop" is a
// rectangle of the instrument's face, in the face's own pixels before scaling.
// "set" puts controls into a state as a person would: a knob by its label or id
// to a value in its range, a button by id to a state index, a menu by id to an
// option index. Every shot starts from a fresh instance in its mode, so shots
// do not depend on each other. The samples are not needed: the face, the
// keyboard and the strips are drawn the same without them.
//
// The plugin draws some text in the system font, so images meant for the
// product page are rendered on the Mac the plugin ships for.

#include "../source/PluginProject.h"

#include <ui/ManifestUiComponent.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <iostream>
#include <memory>
#include <vector>

namespace {

struct Shot {
  juce::String file, mode;
  bool window = false;
  juce::Rectangle<int> crop;
  juce::NamedValueSet set;
};

// The standalone app's window as JUCE's StandaloneFilterWindow builds it: a
// document window with an Options button in its title bar. The editor themes
// it, as it themes the real one.
class ShotWindow final : public juce::DocumentWindow {
public:
  explicit ShotWindow(const juce::String &title)
      : juce::DocumentWindow(title, juce::Colours::black,
                             minimiseButton | closeButton, false) {
    setUsingNativeTitleBar(false);
    Component::addAndMakeVisible(options);
  }

  void resized() override {
    juce::DocumentWindow::resized();
    options.setBounds(8, 6, 60, getTitleBarHeight() - 8);
  }

private:
  juce::TextButton options{"Options"};
};

template <typename T> T *findChild(juce::Component &root) {
  for (auto *child : root.getChildren()) {
    if (auto *t = dynamic_cast<T *>(child))
      return t;
    if (auto *t = findChild<T>(*child))
      return t;
  }
  return nullptr;
}

void hideTitled(juce::Component &root, const juce::String &title) {
  for (auto *child : root.getChildren()) {
    if (child->getTitle() == title)
      child->setVisible(false);
    hideTitled(*child, title);
  }
}

class ScreenshotsApp final : public juce::JUCEApplication, private juce::Timer {
public:
  const juce::String getApplicationName() override {
    return "dmse-screenshots";
  }
  const juce::String getApplicationVersion() override { return "1.0.0"; }
  bool moreThanOneInstanceAllowed() override { return true; }

  void initialise(const juce::String &) override {
    if (!readArguments() || !readRecipe()) {
      setApplicationReturnValue(1);
      quit();
      return;
    }
    startTimerHz(30);
  }

  void shutdown() override {
    stopTimer();
    closeShot();
  }

private:
  bool fail(const juce::String &message) {
    std::cerr << "dmse-screenshots: " << message << "\n";
    return false;
  }

  bool readArguments() {
    const auto args = getCommandLineParameterArray();
    for (int i = 0; i < args.size(); ++i) {
      if (args[i] == "--out" && i + 1 < args.size())
        outDir =
            juce::File::getCurrentWorkingDirectory().getChildFile(args[++i]);
      else if (args[i] == "--only" && i + 1 < args.size())
        only = args[++i];
      else if (repo == juce::File())
        repo = juce::File::getCurrentWorkingDirectory().getChildFile(args[i]);
      else
        return fail("unexpected argument " + args[i]);
    }
    if (!repo.isDirectory())
      return fail(
          "usage: dmse-screenshots <plugin-repo> [--out <folder>] [--only "
          "<file>]");
    if (outDir == juce::File())
      outDir = repo.getChildFile("Screenshots");
    return true;
  }

  bool readRecipe() {
    const auto recipeFile =
        repo.getChildFile("Screenshots").getChildFile("screenshots.json");
    const auto recipe = juce::JSON::parse(recipeFile);
    if (!recipe.isObject())
      return fail("no readable recipe at " + recipeFile.getFullPathName());

    scale = (float)(double)recipe.getProperty("scale", 2.0);
    if (auto *list = recipe.getProperty("shots", {}).getArray()) {
      for (const auto &s : *list) {
        Shot shot;
        shot.file = s.getProperty("file", {}).toString();
        shot.mode = s.getProperty("mode", {}).toString();
        shot.window = (bool)s.getProperty("window", false);
        const auto crop = s.getProperty("crop", {});
        if (crop.isObject())
          shot.crop = {(int)crop.getProperty("x", 0),
                       (int)crop.getProperty("y", 0),
                       (int)crop.getProperty("width", 0),
                       (int)crop.getProperty("height", 0)};
        if (auto *set = s.getProperty("set", {}).getDynamicObject())
          shot.set = set->getProperties();
        if (shot.file.isEmpty() || (!shot.window && shot.crop.isEmpty()))
          return fail("a shot needs a file, and a crop unless it is a window");
        if (only.isEmpty() || only == shot.file)
          shots.push_back(shot);
      }
    }
    if (shots.empty())
      return fail("the recipe has no shots" +
                  (only.isNotEmpty() ? " called " + only : juce::String()));
    outDir.createDirectory();
    return true;
  }

  // One step per tick: open, wait for the mode to load, set the controls, wait
  // for the editor to show them, save.
  void timerCallback() override {
    if (next >= shots.size()) {
      stopTimer();
      quit();
      return;
    }
    const auto &shot = shots[next];
    ++ticks;

    switch (stage) {
    case Stage::open:
      if (!openShot(shot))
        return abandon();
      stage = Stage::loading;
      ticks = 0;
      return;

    case Stage::loading:
      if (project->getProcessor().isLoading() || ticks < 10)
        return;
      if (!applySettings(shot))
        return abandon();
      stage = Stage::settling;
      ticks = 0;
      return;

    case Stage::settling:
      if (ticks < 10)
        return;
      if (!save(shot))
        return abandon();
      closeShot();
      ++next;
      stage = Stage::open;
      return;
    }
  }

  bool openShot(const Shot &shot) {
    juce::String error;
    project = dmse_studio::PluginProject::open(repo, error);
    if (project == nullptr)
      return fail(error);

    auto &processor = project->getProcessor();
    const auto modes = processor.getModeNames();
    int modeIndex = shot.mode.isEmpty() ? 0 : modes.indexOf(shot.mode);
    if (modeIndex < 0)
      return fail(shot.file + ": no mode called \"" + shot.mode +
                  "\", the modes are " + modes.joinIntoString(", "));
    // The editor themes the window only when it may treat it as its own.
    processor.setEditorManagesWindow(shot.window);
    processor.confirmModeChoice(juce::jmax(0, modeIndex));

    editor.reset(processor.createEditor());
    if (shot.window) {
      window = std::make_unique<ShotWindow>(project->getName());
      window->setContentNonOwned(editor.get(), true);
    }
    return true;
  }

  bool applySettings(const Shot &shot) {
    hideTitled(*editor, "Samples not found");
    auto *face = findChild<dm::ManifestUiComponent>(*editor);
    if (face == nullptr)
      return fail(shot.file + ": the editor has no face");
    for (const auto &setting : shot.set)
      if (!face->setValueById(setting.name.toString(), (double)setting.value))
        return fail(shot.file + ": nothing on the face is called \"" +
                    setting.name.toString() + "\"");
    return true;
  }

  bool save(const Shot &shot) {
    juce::Image image;
    if (shot.window) {
      image = window->createComponentSnapshot(window->getLocalBounds(), true,
                                              scale);
    } else {
      auto *face = findChild<dm::ManifestUiComponent>(*editor);
      if (face == nullptr)
        return fail(shot.file + ": the editor has no face");
      // From the editor rather than the face, so a layer drawn over the face,
      // such as an instrument's overlay, is in the picture as it is on screen.
      const auto area = editor->getLocalArea(face, shot.crop);
      image = editor->createComponentSnapshot(area, true, scale);
    }

    const auto file = outDir.getChildFile(shot.file);
    file.deleteFile();
    juce::FileOutputStream out(file);
    if (!out.openedOk() ||
        !juce::PNGImageFormat().writeImageToStream(image, out))
      return fail("could not write " + file.getFullPathName());
    std::cout << shot.file << "  " << image.getWidth() << " x "
              << image.getHeight() << "\n";
    return true;
  }

  void closeShot() {
    if (window != nullptr)
      window->clearContentComponent();
    window.reset();
    editor.reset();
    project.reset();
  }

  void abandon() {
    stopTimer();
    closeShot();
    setApplicationReturnValue(1);
    quit();
  }

  enum class Stage { open, loading, settling };

  juce::File repo, outDir;
  juce::String only;
  float scale = 2.0f;
  std::vector<Shot> shots;
  size_t next = 0;
  Stage stage = Stage::open;
  int ticks = 0;

  std::unique_ptr<dmse_studio::PluginProject> project;
  std::unique_ptr<juce::AudioProcessorEditor> editor;
  std::unique_ptr<ShotWindow> window;
};

} // namespace

START_JUCE_APPLICATION(ScreenshotsApp)
