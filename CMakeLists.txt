# dehli-musikk-sampler-plugin-editor — "DMSE Studio", the desktop authoring app.
#
# A JUCE GUI app that embeds the real sampler engine: it opens a plugin repo from
# disk (assets/manifest + images + IRs + sample pack), instantiates the actual
# ManifestPluginProcessor from those files, and shows/plays the genuine plugin —
# live preview with no compile step. Editing layers (inspector, GUI designer,
# bindings, sample import, build/export) grow on top milestone by milestone.
#
# Built as part of the superproject (which provides JUCE + the engine target).

juce_add_gui_app(DmseStudio
    PRODUCT_NAME "DMSE Studio"
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
