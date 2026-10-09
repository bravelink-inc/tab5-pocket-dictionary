cmake_minimum_required(VERSION 3.16)

# M5Stack Tab5 pocket dictionary firmware (ESP-IDF, ESP32-P4)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pocketdictionary)

# ---------------------------------------------------------------------------
# Built-in dictionary image (flashed to the "dict" partition by `idf.py flash`)
# ---------------------------------------------------------------------------
set(PD_ROOT      "${CMAKE_CURRENT_LIST_DIR}/..")
set(PD_DICT_BIN  "${PD_ROOT}/data/ejdict.pdc")
set(PD_DICT_TOOL "${PD_ROOT}/tools/build_dict.py")
file(GLOB PD_EJDICT_SRC CONFIGURE_DEPENDS "${PD_ROOT}/third_party/ejdict/src/*.txt")

if(NOT PYTHON)
    set(PYTHON python3)   # idf.py passes -DPYTHON=<its venv python>; "python3" does not exist on Windows
endif()
if(NOT EXISTS "${PD_DICT_BIN}" AND PD_EJDICT_SRC)
    message(STATUS "Generating built-in dictionary image: ${PD_DICT_BIN} (with ${PYTHON})")
    execute_process(
        COMMAND "${PYTHON}" "${PD_DICT_TOOL}" --title "EJDict-hand 英和" -o "${PD_DICT_BIN}" ${PD_EJDICT_SRC}
        RESULT_VARIABLE pd_rc)
    if(NOT pd_rc EQUAL 0)
        message(WARNING "build_dict.py failed (rc=${pd_rc}); the dict partition will not be flashed")
    endif()
endif()

if(PD_EJDICT_SRC)
    add_custom_command(OUTPUT "${PD_DICT_BIN}"
        COMMAND "${PYTHON}" "${PD_DICT_TOOL}" --title "EJDict-hand 英和"
                -o "${PD_DICT_BIN}" ${PD_EJDICT_SRC}
        DEPENDS "${PD_DICT_TOOL}" ${PD_EJDICT_SRC}
        VERBATIM)
    add_custom_target(pd_builtin_dictionary ALL DEPENDS "${PD_DICT_BIN}")
    add_dependencies(flash pd_builtin_dictionary)
endif()

if(EXISTS "${PD_DICT_BIN}")
    esptool_py_flash_to_partition(flash "dict" "${PD_DICT_BIN}")
    message(STATUS "dict partition image: ${PD_DICT_BIN}")
else()
    message(WARNING "No dictionary image at ${PD_DICT_BIN}; only SD-card dictionaries will be available")
endif()

# ---------------------------------------------------------------------------
# Japanese font image (flashed to the "font" partition by `idf.py flash`).
# Made by tools/make_font.py and kept in the repository, because building it needs extra tools.
# Without it the firmware falls back to the built-in font (some rare kanji show as boxes).
# ---------------------------------------------------------------------------
set(PD_FONT_BIN "${PD_ROOT}/third_party/notosansjp/jp_fonts.pfn")
if(EXISTS "${PD_FONT_BIN}")
    esptool_py_flash_to_partition(flash "font" "${PD_FONT_BIN}")
    message(STATUS "font partition image: ${PD_FONT_BIN}")
endif()
