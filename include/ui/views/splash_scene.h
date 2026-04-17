#ifndef _UI_VIEWS_SPLASH_SCENE_H_
#define _UI_VIEWS_SPLASH_SCENE_H_

struct View;

// userdata must point to SplashUserdata (caller sets version before push).
typedef struct SplashUserdata {
  char lineBuf[128];
  const char *version;
} SplashUserdata;

struct View *splashGetView(void);

#endif
