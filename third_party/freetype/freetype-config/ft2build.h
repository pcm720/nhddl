/*
 * Custom ft2build.h to make FreeType pick up local config
 */
#ifndef FT2BUILD_H_
#define FT2BUILD_H_

#define FT_CONFIG_MODULES_H  <ftmodule.h>
#define FT_CONFIG_OPTIONS_H  <ftoption.h>

#include <freetype/config/ftheader.h>

#endif /* FT2BUILD_H_ */
