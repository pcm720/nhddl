# Slim FreeType 2.14.1 for NHDDL (TrueType + sfnt + smooth + autofit only).
#
# FreeType sources come from the git submodule at ./freetype/
# Custom config headers are in freetype-config/, placed first
# on the include path so it overrides the stock headers

cmake_minimum_required(VERSION 3.13)
project(freetype_nhddl C)

set(FT_SRC ${CMAKE_CURRENT_SOURCE_DIR}/freetype/src)

set(FT_SOURCES
  ${FT_SRC}/autofit/autofit.c
  ${FT_SRC}/base/ftbase.c
  ${FT_SRC}/base/ftbitmap.c
  ${FT_SRC}/base/ftinit.c
  ${FT_SRC}/base/ftsystem.c
  ${FT_SRC}/base/ftdebug.c
  ${FT_SRC}/sfnt/sfnt.c
  ${FT_SRC}/smooth/smooth.c
  ${FT_SRC}/truetype/truetype.c
)

add_library(freetype STATIC ${FT_SOURCES})

target_compile_definitions(freetype PRIVATE FT2_BUILD_LIBRARY)

# Prefer NHDDL slim ft2build.h / ftmodule.h / ftoption.h (freetype-config/) over
# the stock headers inside the freetype submodule (freetype/include).
target_compile_options(freetype PRIVATE -Wall)
target_include_directories(freetype
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/freetype-config>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/freetype/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/freetype-config
    ${CMAKE_CURRENT_SOURCE_DIR}/freetype/include
)
