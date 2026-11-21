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
// A shot can have a "scale" of its own, such as 1.3333 for a face drawn at
// three quarters of its artwork, so the image shows the artwork's own pixels.
// "overlay": false leaves the instrument's overlay, such as scan lines, out of
// a crop. "strip": { "id": "btn_1", "count": 2, "gap": 24 } puts the crop in
// each of a button's first count states side by side, gap face pixels apart
// with the gap left transparent, to show every choice the button offers.
// "settings": true opens the Settings panel, as its button does. "spotlight"
// is a rectangle of the face, in the same pixels as "crop", left as it is
// while the rest of the crop is blurred and dimmed, to point at one thing.
// "set" puts controls into a state as a person would: a knob by its label or id
// to a value in its range, a button by id to a state index, a menu by id to an
// option index. A name the face does not have is taken as the id of one of the
// plugin's own parameters, such as "pitchDrift" and "volumeDrift" for the drift
// wheels beside the keyboard, set to a value in its range. Failing that, it is
// the name a screen reader gives one of the editor's own sliders, such as "Mod
// wheel", set to a value in the slider's range. Every shot starts from a fresh instance in its mode, so shots
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
  float scale = 0.0f; // 0 is the recipe's scale
  bool overlay = true;
  bool settings = false;
  juce::Rectangle<int> spotlight;
  juce::String strip;
  int stripCount = 0, stripGap = 0;
};

// The standalone app's window as JUCE's StandaloneFilterWindow builds it: a
// document window with an Options button at the left of its title bar and the
// window buttons at the right on every system. The editor themes it, as it
// themes the real one.
class ShotWindow final : public juce::DocumentWindow {
public:
  explicit ShotWindow(const juce::String &title)
      : juce::DocumentWindow(title, juce::Colours::black,
                             minimiseButton | closeButton, false) {
    setUsingNativeTitleBar(false);
    setTitleBarButtonsRequired(minimiseButton | closeButton, false);
    Component::addAndMakeVisible(options);
  }

  void resized() override {
    juce::DocumentWindow::resized();
    options.setBounds(8, 6, 60, getTitleBarHeight() - 8);
  }

private:
  juce::TextButton options{"Options"};
};

// Blurs the picture by about a face pixel and dims it to half, except inside
// the area, which keeps its own pixels within slightly rounded corners.
juce::Image spotlit(const juce::Image &sharp, juce::Rectangle<float> area,
                    float scale) {
  auto image = sharp.createCopy();
  juce::ImageConvolutionKernel blur(juce::roundToInt(scale * 6.0f) | 1);
  blur.createGaussianBlur(scale);
  blur.applyToImage(image, sharp, image.getBounds());
  juce::Graphics g(image);
  g.setColour(juce::Colours::black.withAlpha(0.5f));
  g.fillAll();
  juce::Path box;
  box.addRoundedRectangle(area, scale * 1.5f);
  g.reduceClipRegion(box);
  g.drawImageAt(sharp, 0, 0);
  return image;
}

template <typename T> T *findChild(juce::Component &root) {
  for (auto *child : root.getChildren()) {
    if (auto *t = dynamic_cast<T *>(child))
      return t;
    if (auto *t = findChild<T>(*child))
      return t;
  }
  return nullptr;
}

juce::Button *findButton(juce::Component &root, const juce::String &text) {
  for (auto *child : root.getChildren()) {
    if (auto *button = dynamic_cast<juce::Button *>(child);
        button != nullptr && button->getButtonText() == text)
      return button;
    if (auto *button = findButton(*child, text))
      return button;
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
        shot.scale = (float)(double)s.getProperty("scale", 0.0);
        shot.overlay = (bool)s.getProperty("overlay", true);
        shot.settings = (bool)s.getProperty("settings", false);
        const auto spotlight = s.getProperty("spotlight", {});
        if (spotlight.isObject())
          shot.spotlight = {(int)spotlight.getProperty("x", 0),
                            (int)spotlight.getProperty("y", 0),
                            (int)spotlight.getProperty("width", 0),
                            (int)spotlight.getProperty("height", 0)};
        const auto strip = s.getProperty("strip", {});
        if (strip.isObject()) {
          shot.strip = strip.getProperty("id", {}).toString();
          shot.stripCount = (int)strip.getProperty("count", 0);
          shot.stripGap = (int)strip.getProperty("gap", 0);
          if (shot.strip.isEmpty() || shot.stripCount < 1)
            return fail(shot.file + ": a strip needs an id and a count");
        }
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
      if (!face->setValueById(setting.name.toString(),
                              (double)setting.value) &&
          !setParameter(setting.name.toString(), (float)setting.value) &&
          !setSlider(setting.name.toString(), (double)setting.value))
        return fail(shot.file + ": nothing on the face, among the plugin's "
                                "parameters or among the editor's sliders is "
                                "called \"" +
                    setting.name.toString() + "\"");
    if (shot.settings) {
      auto *button = findButton(*editor, "Settings");
      if (button == nullptr || !button->onClick)
        return fail(shot.file + ": the editor has no Settings button");
      button->onClick();
    }
    return true;
  }

  bool setSlider(const juce::String &title, double value) {
    std::function<juce::Slider *(juce::Component &)> find =
        [&](juce::Component &root) -> juce::Slider * {
      for (auto *child : root.getChildren()) {
        if (auto *slider = dynamic_cast<juce::Slider *>(child);
            slider != nullptr && slider->getTitle() == title)
          return slider;
        if (auto *slider = find(*child))
          return slider;
      }
      return nullptr;
    };
    auto *slider = find(*editor);
    if (slider == nullptr)
      return false;
    slider->setValue(value, juce::sendNotificationSync);
    return true;
  }

  bool setParameter(const juce::String &id, float value) {
    for (auto *parameter : project->getProcessor().getParameters())
      if (auto *ranged = dynamic_cast<juce::RangedAudioParameter *>(parameter);
          ranged != nullptr && ranged->getParameterID() == id) {
        ranged->setValueNotifyingHost(ranged->convertTo0to1(value));
        return true;
      }
    return false;
  }

  bool save(const Shot &shot) {
    const auto shotScale = shot.scale > 0.0f ? shot.scale : scale;
    juce::Image image;
    if (shot.window) {
      image = window->createComponentSnapshot(window->getLocalBounds(), true,
                                              shotScale);
    } else {
      auto *face = findChild<dm::ManifestUiComponent>(*editor);
      if (face == nullptr)
        return fail(shot.file + ": the editor has no face");
      const auto snapshot = [&] {
        // From the editor rather than the face, so a layer drawn over the
        // face, such as an instrument's overlay, is in the picture as it is
        // on screen. Without the overlay, from the face alone, which leaves
        // out such a layer too.
        if (!shot.overlay)
          return face->createComponentSnapshot(shot.crop, true, shotScale);
        const auto area = editor->getLocalArea(face, shot.crop);
        return editor->createComponentSnapshot(area, true, shotScale);
      };
      face->setOverlayShown(shot.overlay);
      if (shot.strip.isEmpty()) {
        image = snapshot();
        if (!shot.spotlight.isEmpty())
          image = spotlit(image,
                          (shot.spotlight - shot.crop.getPosition()).toFloat() *
                              shotScale,
                          shotScale);
      } else {
        std::vector<juce::Image> states;
        for (int state = 0; state < shot.stripCount; ++state) {
          if (!face->setValueById(shot.strip, state))
            return fail(shot.file + ": nothing on the face is called \"" +
                        shot.strip + "\"");
          states.push_back(snapshot());
        }
        const auto gap = juce::roundToInt((float)shot.stripGap * shotScale);
        int width = gap * (shot.stripCount - 1), height = 0;
        for (const auto &state : states) {
          width += state.getWidth();
          height = juce::jmax(height, state.getHeight());
        }
        image = juce::Image(juce::Image::ARGB, width, height, true);
        juce::Graphics g(image);
        int x = 0;
        for (const auto &state : states) {
          g.drawImageAt(state, x, 0);
          x += state.getWidth() + gap;
        }
      }
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
