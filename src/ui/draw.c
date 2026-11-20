#include "ui/draw.h"
#include "ui/font.h"
#include "ui/layout.h"
#include "ui/scale.h"
#include <gsKit.h>
#include <gsPrimitive.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define ARC_SEGMENTS 48
#define ARC_MAX_VERTS (1 + ARC_SEGMENTS + 1)

void uiDrawRect(GSGLOBAL *gs, int vx1, int vy1, int vx2, int vy2, int z, uint64_t color) {
  int x1 = scaleScaleX(vx1) + scaleGetOffsetX();
  int y1 = scaleScaleY(vy1) + scaleGetOffsetY();
  int x2 = scaleScaleX(vx2) + scaleGetOffsetX();
  int y2 = scaleScaleY(vy2) + scaleGetOffsetY();
  gsKit_prim_sprite(gs, (float)x1, (float)y1, (float)x2, (float)y2, z, color);
}

void uiDrawRectNative(GSGLOBAL *gs, int nx1, int ny1, int nx2, int ny2, int z, uint64_t color) {
  gsKit_prim_sprite(gs, (float)nx1, (float)ny1, (float)nx2, (float)ny2, z, color);
}

// Filled arc (triangle fan). Center and radius in virtual coords; angles in degrees.
// Based on ps2sdk draw_arc_filled
static void arcFilled(GSGLOBAL *gs, int cx_v, int cy_v, int radius_v, float angle_start_deg, float angle_end_deg, int z, uint64_t color) {
  float cx_s = (float)(scaleScaleX(cx_v) + scaleGetOffsetX());
  float cy_s = (float)(scaleScaleY(cy_v) + scaleGetOffsetY());
  float rx = (float)scaleScaleX(radius_v);
  float ry = (float)scaleScaleY(radius_v);
  int segs = ARC_SEGMENTS;
  if (segs < 1)
    segs = 1;
  float step = (angle_end_deg - angle_start_deg) / (float)segs;
  static float triFan[2 * ARC_MAX_VERTS];
  int n = 0;
  triFan[n++] = cx_s;
  triFan[n++] = cy_s;
  for (int i = 0; i <= segs; i++) {
    float deg = angle_start_deg + step * (float)i;
    float rad = deg * (float)(3.141592653589793 / 180.0);
    float c = cosf(rad);
    float s = sinf(rad);
    triFan[n++] = cx_s + rx * c;
    triFan[n++] = cy_s + ry * s;
  }
  gsKit_prim_triangle_fan(gs, triFan, 1 + segs + 1, z, color);
}

void uiDrawRectRounded(GSGLOBAL *gs, int vx1, int vy1, int vx2, int vy2, int radius_v, int z, uint64_t color) {
  int w = vx2 - vx1;
  int h = vy2 - vy1;
  if (w <= 0 || h <= 0)
    return;
  int r = radius_v;
  if (r > w / 2)
    r = w / 2;
  if (r > h / 2)
    r = h / 2;
  if (r <= 0) {
    uiDrawRect(gs, vx1, vy1, vx2, vy2, z, color);
    return;
  }

  int cx1 = vx1 + r;
  int cy1 = vy1 + r;
  int cx2 = vx2 - r;
  int cy2 = vy2 - r;
  // Center rectangle
  uiDrawRect(gs, cx1, cy1, cx2, cy2, z, color);
  // Top/bottom/left/right strips
  uiDrawRect(gs, cx1, vy1, cx2, cy1, z, color);
  uiDrawRect(gs, cx1, cy2, cx2, vy2, z, color);
  uiDrawRect(gs, vx1, cy1, cx1, cy2, z, color);
  uiDrawRect(gs, cx2, cy1, vx2, cy2, z, color);
  // Arcs
  arcFilled(gs, cx2, cy1, r, 270.0f, 360.0f, z, color); // top right
  arcFilled(gs, cx2, cy2, r, 0.0f, 90.0f, z, color);    // bottom right
  arcFilled(gs, cx1, cy2, r, 90.0f, 180.0f, z, color);  // bottom left
  arcFilled(gs, cx1, cy1, r, 180.0f, 270.0f, z, color); // top left
}

// Full circle with uniform radius (uses scaleY for both axes so it stays round in widescreen).
static void circleFilledUniform(GSGLOBAL *gs, int cx_v, int cy_v, int radius_v, int z, uint64_t color) {
  if (radius_v <= 0)
    return;
  float cx_s = (float)(scaleScaleX(cx_v) + scaleGetOffsetX());
  float cy_s = (float)(scaleScaleY(cy_v) + scaleGetOffsetY());
  float r_uniform = (float)radius_v * scaleGetScaleY();
  int segs = ARC_SEGMENTS;
  float step = 360.0f / (float)segs;
  static float triFan[2 * ARC_MAX_VERTS];
  int n = 0;
  triFan[n++] = cx_s;
  triFan[n++] = cy_s;
  for (int i = 0; i <= segs; i++) {
    float deg = step * (float)i;
    float rad = deg * (float)(3.141592653589793 / 180.0);
    float c = cosf(rad);
    float s = sinf(rad);
    triFan[n++] = cx_s + r_uniform * c;
    triFan[n++] = cy_s + r_uniform * s;
  }
  gsKit_prim_triangle_fan(gs, triFan, 1 + segs + 1, z, color);
}

void uiDrawCircleFilled(GSGLOBAL *gs, int cx_v, int cy_v, int radius_v, int z, uint64_t color) {
  if (radius_v <= 0)
    return;
  circleFilledUniform(gs, cx_v, cy_v, radius_v, z, color);
}

int uiSelectionIndicatorCenterY(int rowY, int rowH) { return rowY + rowH / 2 + UI_SEL_INDICATOR_OPTICAL_NUDGE; }

// Draw a selection indicator: small filled circle when selected, nothing when not. Center (vx, vy) and radius_v in virtual coords.
void uiDrawSelectionIndicator(GSGLOBAL *gs, int vx, int vy, int radius_v, int z, uint64_t color, int selected) {
  if (selected)
    uiDrawCircleFilled(gs, vx, vy, radius_v, z, color);
}

void uiDrawChoiceRow(GSGLOBAL *gs, int leftX, int rightX, int entryY, int rowH, int z, uint64_t color, int on, const char *label) {
  uiDrawSelectionIndicator(gs, leftX + UI_SEL_INDICATOR_R, uiSelectionIndicatorCenterY(entryY, rowH), UI_SEL_INDICATOR_R, z, color, on);
  fontRenderInRect(FONT_DEFAULT, leftX + UI_SEL_LABEL_INDENT, entryY, rightX, entryY + rowH, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, z + 1,
                   label ? label : "", color);
}

void uiTrimPathMiddle(const char *src, char *out, size_t outSize, const char *emptyLabel) {
  if (!out || outSize == 0)
    return;
  if (!src || !src[0]) {
    snprintf(out, outSize, "%s", emptyLabel ? emptyLabel : "");
    return;
  }
  size_t len = strlen(src);
  if (len < outSize) {
    snprintf(out, outSize, "%s", src);
    return;
  }
  if (outSize < 8) {
    out[0] = '\0';
    return;
  }
  size_t head = (outSize - 4) / 2;
  size_t tail = outSize - 4 - head;
  snprintf(out, outSize, "%.*s...%s", (int)head, src, src + len - tail);
}

int uiContentPanelWidth(void) {
  int panelW = SCENE_VW - 4 * SCENE_KEEPOUT;
  if (panelW < 320)
    panelW = SCENE_VW - 2 * SCENE_KEEPOUT;
  return panelW;
}

int uiContentPanelLeft(void) {
  int panelW = uiContentPanelWidth();
  return (SCENE_VW - panelW) / 2;
}

void uiDrawIcon(GSGLOBAL *gs, int vx, int vy, int z, uint64_t color, IconType iconType) {
  float x = (float)(scaleScaleX(vx) + scaleGetOffsetX());
  float y = (float)(scaleScaleY(vy) + scaleGetOffsetY());
  // Uniform scale (scaleY) for both dimensions so icons keep aspect and don't look stretched in widescreen.
  float scaleU = scaleGetScaleY();
  float w = (float)(int)((float)getIconWidth(iconType) * scaleU + 0.5f);
  float h = (float)(int)((float)getIconHeight(iconType) * scaleU + 0.5f);
  drawIconAt(gs, x, y, w, h, z, color, iconType);
}

// Draw icon at (vx, vy) with virtual size (vw, vh). Used for prompts. Uses uniform scale (scaleY) for both
// dimensions so icons keep aspect in widescreen and don't appear too wide.
void uiDrawIconScaled(GSGLOBAL *gs, int vx, int vy, int vw, int vh, int z, uint64_t color, IconType iconType) {
  float x = (float)(scaleScaleX(vx) + scaleGetOffsetX());
  float y = (float)(scaleScaleY(vy) + scaleGetOffsetY());
  float scaleU = scaleGetScaleY();
  float w = (float)(int)((float)vw * scaleU + 0.5f);
  float h = (float)(int)((float)vh * scaleU + 0.5f);
  if (w < 1.0f)
    w = 1.0f;
  if (h < 1.0f)
    h = 1.0f;
  drawIconAt(gs, x, y, w, h, z, color, iconType);
}

#define PROMPT_ICON_TEXT_GAP 4
#define PROMPT_ICON_ICON_GAP 3
#define PROMPT_ICON_SIZE_V 22
// Font VCENTER is baseline-centric, so drawn text sits low in the rect. Offset text rect up so it aligns with icon.
#define PROMPT_TEXT_SLOT_OFFSET_UP 3

static void promptIconVirtualSize(IconType iconType, int *outW, int *outH) {
  int iconW_nat = getIconWidth(iconType);
  int iconH_nat = getIconHeight(iconType);
  int iconH_tgt = PROMPT_ICON_SIZE_V;
  int iconMax = iconW_nat > iconH_nat ? iconW_nat : iconH_nat;
  int iconW_v = (iconH_tgt * iconW_nat + iconMax / 2) / iconMax;
  int iconH_v = (iconH_tgt * iconH_nat + iconMax / 2) / iconMax;
  if (iconW_v < 1)
    iconW_v = 1;
  if (iconH_v < 1)
    iconH_v = 1;
  *outW = iconW_v;
  *outH = iconH_v;
}

// Will draw a single-icon prompt if iconType2 == ICON_COUNT
static void uiDrawPrompt(GSGLOBAL *gs, int x, int y, int z, uint64_t color, IconType iconType, IconType iconType2, const char *label) {
  int iconW_v, iconH_v;
  promptIconVirtualSize(iconType, &iconW_v, &iconH_v);
  int iconsW = PROMPT_ICON_SIZE_V;
  int hasSecond = (iconType2 < ICON_COUNT);
  int icon2W_v = 0, icon2H_v = 0;
  if (hasSecond) {
    promptIconVirtualSize(iconType2, &icon2W_v, &icon2H_v);
    iconsW = PROMPT_ICON_SIZE_V + PROMPT_ICON_ICON_GAP + PROMPT_ICON_SIZE_V;
  }

  int textW_nat = fontCalcDimensionsFirstLine(FONT_PROMPT, label);
  int textW_v = scaleUnscaleX(textW_nat);
  int totalW_v = iconsW + PROMPT_ICON_TEXT_GAP + textW_v;
  int leftX = x - totalW_v / 2;
  int slotHalfH = SCENE_ROW_SIZE / 2;

  int iconTopV = y - iconH_v / 2;
  int iconLeftV = leftX + (PROMPT_ICON_SIZE_V - iconW_v) / 2;
  uiDrawIconScaled(gs, iconLeftV, iconTopV, iconW_v, iconH_v, z, color, iconType);
  if (hasSecond) {
    int icon2TopV = y - icon2H_v / 2;
    int icon2LeftV = leftX + PROMPT_ICON_SIZE_V + PROMPT_ICON_ICON_GAP + (PROMPT_ICON_SIZE_V - icon2W_v) / 2;
    uiDrawIconScaled(gs, icon2LeftV, icon2TopV, icon2W_v, icon2H_v, z, color, iconType2);
  }

  int textTop = y - slotHalfH - PROMPT_TEXT_SLOT_OFFSET_UP;
  int textBottom = y + slotHalfH - PROMPT_TEXT_SLOT_OFFSET_UP;
  int tx1 = leftX + iconsW + PROMPT_ICON_TEXT_GAP;
  int tx2 = leftX + totalW_v;
  fontRenderInRect(FONT_PROMPT, tx1, textTop, tx2, textBottom, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, z, label, color);
}

void uiDrawScenePrompt(GSGLOBAL *gs, int slot, int z, uint64_t color, IconType iconType, const char *label) {
  uiDrawPrompt(gs, SCENE_PROMPT_SLOT_CENTER_X(slot), SCENE_FOOTER_ROW_Y, z, color, iconType, ICON_COUNT, label);
}

void uiDrawScenePrompt2(GSGLOBAL *gs, int slot, int z, uint64_t color, IconType iconType, IconType iconType2, const char *label) {
  uiDrawPrompt(gs, SCENE_PROMPT_SLOT_CENTER_X(slot), SCENE_FOOTER_ROW_Y, z, color, iconType, iconType2, label);
}

// Draw texture in virtual rect; ratio-preserving and centered. If tex is NULL or empty, draws solid rect.
void uiDrawTextureInVirtualRect(GSGLOBAL *gs, int vx, int vy, int vw, int vh, GSTEXTURE *tex, int z, uint64_t color, int disableAlphaTest) {
  int slotX1 = scaleScaleX(vx) + scaleGetOffsetX();
  int slotY1 = scaleScaleY(vy) + scaleGetOffsetY();
  int slotX2 = scaleScaleX(vx + vw) + scaleGetOffsetX();
  int slotY2 = scaleScaleY(vy + vh) + scaleGetOffsetY();
  float scaleXR = scaleGetScaleXForRatio();
  float scaleYR = scaleGetScaleY();
  int destW = (int)((float)vw * scaleXR + 0.5f);
  int destH = (int)((float)vh * scaleYR + 0.5f);
  int drawX = (slotX1 + slotX2 - destW) / 2;
  int drawY = (slotY1 + slotY2 - destH) / 2;
  if (disableAlphaTest)
    gs->PrimAlphaEnable = GS_SETTING_OFF;
  if (tex && tex->Width > 0 && tex->Height > 0) {
    gsKit_prim_sprite_texture(gs, tex, (float)drawX, (float)drawY, 0.0f, 0.0f, (float)(drawX + destW), (float)(drawY + destH), (float)tex->Width,
                              (float)tex->Height, z, color);
  } else {
    uiDrawRectNative(gs, drawX, drawY, drawX + destW, drawY + destH, z, color);
  }
  if (disableAlphaTest)
    gs->PrimAlphaEnable = GS_SETTING_ON;
}
