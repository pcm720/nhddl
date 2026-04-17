#include "ui/draw.h"
#include "ui/font.h"
#include "ui/icons.h"
#include "ui/layout.h"
#include "ui/ui_shared.h"
#include "ui/view.h"
#include "ui/views/splash_scene.h"
#include "ui/worker/worker.h"
#include "dprintf.h"
#include <gsToolkit.h>
#include <stdio.h>
#include <stdlib.h>

#define SPLASH_FRAMES 90

// Embedded splash PNG (res/icons/splash.png via embed.cmake)
extern unsigned char icon_splash[] __attribute__((aligned(16)));
extern unsigned int size_icon_splash;

static View s_view;
static int s_framesLeft;
static GSTEXTURE *s_splashTexture;

static void splashOnEnter(View *v) {
  (void)v;
  s_framesLeft = SPLASH_FRAMES;
  s_splashTexture = calloc(1, sizeof(GSTEXTURE));
  if (s_splashTexture && loadPNGFromMemory(gsGlobal, s_splashTexture, icon_splash, size_icon_splash) != 0) {
    free(s_splashTexture);
    s_splashTexture = NULL;
  }
  if (s_splashTexture)
    s_splashTexture->Filter = GS_FILTER_LINEAR;
}

static void splashOnLeave(View *v) {
  (void)v;
  if (s_splashTexture) {
    gsKit_TexManager_invalidate(gsGlobal, s_splashTexture);
    gsKit_TexManager_free(gsGlobal, s_splashTexture);
    if (s_splashTexture->Mem)
      free(s_splashTexture->Mem);
    free(s_splashTexture);
    s_splashTexture = NULL;
  }
}

static void splashDraw(View *v, int zOrder) {
  SplashUserdata *u = (SplashUserdata *)v->userdata;
  const int centerVX = SCENE_VW / 2;
  const int logoTopVY = SCENE_VH / 4;

  if (s_splashTexture && s_splashTexture->Width > 0 && s_splashTexture->Height > 0) {
    int w = s_splashTexture->Width;
    int h = s_splashTexture->Height;
    int leftV = centerVX - w / 2;
    uiDrawTextureInVirtualRect(gsGlobal, leftV, logoTopVY, w, h, s_splashTexture, zOrder, HeaderTextColor, 0);
    if (u && u->version) {
      snprintf(u->lineBuf, sizeof(u->lineBuf), "%s", u->version);
      fontRenderString(FONT_DEFAULT, centerVX, logoTopVY + h + 10, FONT_ALIGN_HCENTER, 0, 0, zOrder, u->lineBuf, HeaderTextColor);
    }
  }
}

static ViewResult splashOnInput(View *v, int padInput) {
  (void)v;
  s_framesLeft--;
  if (s_framesLeft <= 0 || padInput) {
    if (workerIsListReady() || padInput)
      return ViewResult_Pop;
  }
  return ViewResult_Continue;
}

struct View *splashGetView(void) {
  s_view = (View){
    .type = ViewType_Scene,
    .userdata = NULL,
    .onEnter = splashOnEnter,
    .onLeave = splashOnLeave,
    .draw = splashDraw,
    .onInput = splashOnInput,
  };
  return &s_view;
}
