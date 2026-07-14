# dehli-musikk-sampler-plugin-editor — "DMSE Studio", the desktop authoring app.
#
# A JUCE GUI app that embeds the real sampler engine: it opens a plugin repo from
# disk (assets/manifest + images + IRs + sample pack), instantiates the actual
# ManifestPluginProcessor from those files, and shows/plays the genuine plugin —
# live preview with no compile step. Editing layers (inspector, GUI designer,
# bindings, sample import, build/export) grow on top milestone by milestone.
#
# Built as part of the superproject (which provides JUCE + the engine target).

# App icon by the house convention: drop packaging/icon.png (square, 1024x1024
# recommended) and JUCE bakes it into the bundle (macOS .icns / Windows .ico).
set(_studio_icon_args)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icon.png")
    set(_studio_icon_args ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icon.png")
    message(STATUS "DmseStudio: app icon from packaging/icon.png")
endif()

juce_add_gui_app(DmseStudio
    ${_studio_icon_args}
    PRODUCT_NAME "DMSE Studio"
    VERSION      "1.0.0"
    COMPANY_NAME "DehliMusikk"
    BUNDLE_ID    "com.dehlimusikk.dmsestudio"
)

target_sources(DmseStudio PRIVATE
    source/Main.cpp
    source/StudioMainComponent.cpp
)

target_compile_definitions(DmseStudio PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:DmseStudio,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:DmseStudio,JUCE_VERSION>"
)

target_link_libraries(DmseStudio PRIVATE
    dehli_musikk_sampler_engine
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# Headless probe/test runner (used by development; not shipped).
juce_add_console_app(DmseStudioProbe PRODUCT_NAME "dmse-studio-probe")
target_sources(DmseStudioProbe PRIVATE tests/StudioProbe.cpp)
target_compile_definitions(DmseStudioProbe PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(DmseStudioProbe PRIVATE
    dehli_musikk_sampler_engine
    juce::juce_audio_utils
    juce::juce_recommended_config_flags)
