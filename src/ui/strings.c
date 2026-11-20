#include "ui/strings.h"
#include <string.h>

#include "ui/lang/en.h"

typedef struct {
  const char *code;
  const char *name;
  const char *const *ui;
  const char *const *neutrino;
} UiLanguage;

static const UiLanguage s_langs[] = {
    // English
    {UI_LANG_EN_CODE, UI_LANG_EN_NAME, ui_str_en, neutrino_str_en},
    //
};

static int s_langIndex;

static int langCount(void) { return (int)(sizeof(s_langs) / sizeof(s_langs[0])); }

int uiLanguageCount(void) { return langCount(); }

int uiLanguageIndex(void) {
  if (s_langIndex < 0 || s_langIndex >= langCount())
    return 0;
  return s_langIndex;
}

const char *uiLanguageCode(int index) {
  if (index < 0 || index >= langCount())
    return s_langs[0].code;
  return s_langs[index].code;
}

const char *uiLanguageName(int index) {
  if (index < 0 || index >= langCount())
    return s_langs[0].name;
  return s_langs[index].name;
}

int setUiLanguageByCode(const char *code) {
  if (!code || !code[0])
    return -1;
  for (int i = 0; i < langCount(); i++) {
    if (!strcmp(s_langs[i].code, code)) {
      s_langIndex = i;
      return 0;
    }
  }
  return -1;
}

const char *getUiLanguageCode(void) { return uiLanguageCode(uiLanguageIndex()); }

const char *uiStr(UiStrId id) {
  if ((int)id < 0 || id >= UI_STR_COUNT)
    return "";
  int li = uiLanguageIndex();
  const char *s = s_langs[li].ui[id];
  if (s && s[0])
    return s;
  return ui_str_en[id] ? ui_str_en[id] : "";
}

const char *neutrinoStr(NeutrinoStrId id) {
  if ((int)id < 0 || id >= UI_N_STR_COUNT)
    return "";
  int li = uiLanguageIndex();
  const char *s = s_langs[li].neutrino[id];
  if (s && s[0])
    return s;
  return neutrino_str_en[id] ? neutrino_str_en[id] : "";
}
