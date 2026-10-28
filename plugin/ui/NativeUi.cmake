# The plugin UI (see plugin/docs/native-ui.md). Included by the plugin and
# by the testbed app; both call t3k_add_native_ui(<target>) to compile the
# backend-agnostic UI sources into themselves. The plugin adds
# ProcessorBackend/NativeEditor on top, the testbed adds MockBackend.

set(T3K_UI_DIR "${CMAKE_CURRENT_LIST_DIR}")

# Embedded fonts (Roboto Mono and Arimo, both Apache-2.0; Shrikhand and
# Varela Round for the Ether skin, both SIL OFL 1.1, from google/fonts at
# ddf386ba, licences alongside) and brand artwork,
# in their own namespace/header (UiBinaryData) so they read as UI assets.
# Built-in skins: one folder per skin with its skin.json manifest (the same
# format users drop into their skins folder, see SKINS.md) and an optional
# bg.svg. Copied under unique names first, since binary data symbols come
# from file names.
set(T3K_BUILTIN_SKIN_FILES)
foreach(_skin classic neural minimal vintage burningchurch ether)
    configure_file("${T3K_UI_DIR}/skins/${_skin}/skin.json"
                   "${CMAKE_BINARY_DIR}/t3k_skins/skin_${_skin}.json" COPYONLY)
    list(APPEND T3K_BUILTIN_SKIN_FILES "${CMAKE_BINARY_DIR}/t3k_skins/skin_${_skin}.json")
    if (EXISTS "${T3K_UI_DIR}/skins/${_skin}/bg.svg")
        configure_file("${T3K_UI_DIR}/skins/${_skin}/bg.svg"
                       "${CMAKE_BINARY_DIR}/t3k_skins/skin_${_skin}_bg.svg" COPYONLY)
        list(APPEND T3K_BUILTIN_SKIN_FILES "${CMAKE_BINARY_DIR}/t3k_skins/skin_${_skin}_bg.svg")
    endif()
endforeach()

if (NOT TARGET NativeUiAssets)
    juce_add_binary_data(NativeUiAssets
        NAMESPACE UiBinaryData
        HEADER_NAME UiBinaryData.h
        SOURCES
            "${T3K_UI_DIR}/assets/RobotoMono-Regular.ttf"
            "${T3K_UI_DIR}/assets/RobotoMono-Bold.ttf"
            "${T3K_UI_DIR}/assets/Arimo-Regular.ttf"
            "${T3K_UI_DIR}/assets/Arimo-Bold.ttf"
            "${T3K_UI_DIR}/assets/Arimo-Italic.ttf"
            "${T3K_UI_DIR}/assets/Arimo-BoldItalic.ttf"
            "${T3K_UI_DIR}/assets/Shrikhand-Regular.ttf"
            "${T3K_UI_DIR}/assets/VarelaRound-Regular.ttf"
            "${T3K_UI_DIR}/assets/t3k.svg"
            "${T3K_UI_DIR}/assets/t3k-mark.svg"
            ${T3K_BUILTIN_SKIN_FILES}
    )
    # Plugin formats are shared libraries; the object code needs to be PIC.
    set_target_properties(NativeUiAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# TONE3000 configuration comes from the repo-root .env (and .env.local
# overrides); a variable in the configure environment wins over both (CI
# passes the publishable key that way). See .env.example for the keys.
function(_t3k_read_env key default out)
    set(_value "${default}")
    foreach(_file "${T3K_UI_DIR}/../../.env" "${T3K_UI_DIR}/../../.env.local")
        if (EXISTS "${_file}")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_file}")
            file(STRINGS "${_file}" _lines REGEX "^[ \t]*${key}=")
            foreach(_line ${_lines})
                string(REGEX REPLACE "^[ \t]*${key}=[ \t]*\"?([^\"]*)\"?[ \t]*$" "\\1" _value "${_line}")
            endforeach()
        endif()
    endforeach()
    if (DEFINED ENV{${key}} AND NOT "$ENV{${key}}" STREQUAL "")
        set(_value "$ENV{${key}}")
    endif()
    set(${out} "${_value}" PARENT_SCOPE)
endfunction()

_t3k_read_env(T3K_PUBLISHABLE_KEY "" T3K_PUBLISHABLE_KEY)
_t3k_read_env(T3K_API_DOMAIN "https://www.tone3000.com" T3K_API_DOMAIN)
_t3k_read_env(T3K_UPDATE_NOTICE "false" T3K_UPDATE_NOTICE)
string(REGEX REPLACE "/+$" "" T3K_API_DOMAIN "${T3K_API_DOMAIN}")
if (T3K_UPDATE_NOTICE STREQUAL "true")
    set(T3K_UPDATE_NOTICE 1)
else()
    set(T3K_UPDATE_NOTICE 0)
endif()
configure_file("${T3K_UI_DIR}/core/T3kConfig.h.in" "${CMAKE_BINARY_DIR}/t3k_ui/T3kConfig.h" @ONLY)

file(GLOB_RECURSE T3K_UI_SOURCES CONFIGURE_DEPENDS
    "${T3K_UI_DIR}/core/*.cpp"    "${T3K_UI_DIR}/core/*.h"
    "${T3K_UI_DIR}/model/*.cpp"   "${T3K_UI_DIR}/model/*.h"
    "${T3K_UI_DIR}/backend/Backend.h"
    "${T3K_UI_DIR}/services/*.cpp" "${T3K_UI_DIR}/services/*.h"
    "${T3K_UI_DIR}/widgets/*.cpp" "${T3K_UI_DIR}/widgets/*.h"
    "${T3K_UI_DIR}/views/*.cpp"   "${T3K_UI_DIR}/views/*.h"
    "${T3K_UI_DIR}/views/*/*.cpp" "${T3K_UI_DIR}/views/*/*.h"
    # Third-party code carried in-tree (each with its licence alongside):
    # Nayuki's QR Code generator (MIT), for the sign-in screen's device code.
    "${T3K_UI_DIR}/vendor/*/*.cpp" "${T3K_UI_DIR}/vendor/*/*.hpp"
)

function(t3k_add_native_ui target)
    target_sources(${target} PRIVATE ${T3K_UI_SOURCES})
    target_include_directories(${target} PRIVATE "${T3K_UI_DIR}" "${CMAKE_BINARY_DIR}/t3k_ui")
    target_link_libraries(${target} PRIVATE NativeUiAssets juce::juce_animation juce::juce_cryptography)
    # macOS: paint through a Metal-backed layer so each dirty rect is drawn
    # on its own. Plain CoreGraphics gets one merged rect per frame, so the
    # two meters ticking together would repaint the whole plate between them.
    target_compile_definitions(${target} PRIVATE
        $<$<PLATFORM_ID:Darwin>:JUCE_COREGRAPHICS_RENDER_WITH_MULTIPLE_PAINT_CALLS=1>)
endfunction()
