#ifndef _UI_LAYOUT_H_
#define _UI_LAYOUT_H_

// Virtual screen size
#define SCENE_VW 640
#define SCENE_VH 480
// Screen keepout area (overscan)
#define SCENE_KEEPOUT 20
#define SCENE_ROW_SIZE 28

// Full-screen views (ViewType_Scene): button prompt area at bottom with keepout.
// Content must stay above SCENE_CONTENT_BOTTOM.
#define SCENE_FOOTER_ROW_Y (SCENE_VH - SCENE_KEEPOUT * 2)
#define SCENE_FOOTER_TOP SCENE_FOOTER_ROW_Y
#define SCENE_FOOTER_HEIGHT (SCENE_VH - SCENE_FOOTER_TOP)
#define SCENE_CONTENT_BOTTOM (SCENE_VH - SCENE_FOOTER_HEIGHT - SCENE_ROW_SIZE)

// Scene prompt: row 0 = icons, row 1 = text (same slot = one column, two rows).
#define SCENE_PROMPT_ICON_ROW_Y SCENE_FOOTER_ROW1_Y
#define SCENE_PROMPT_TEXT_ROW_Y SCENE_FOOTER_ROW2_Y

// Scene: four fixed-width slots in the keepout strip (640 - 2*keepout = 600 wide, 150 per slot).
#define SCENE_PROMPT_STRIP_WIDTH (SCENE_VW - 2 * SCENE_KEEPOUT)
#define SCENE_PROMPT_SLOT_WIDTH 150
#define SCENE_PROMPT_SLOT_CENTER_X(slotIndex) (SCENE_KEEPOUT + (slotIndex) * SCENE_PROMPT_SLOT_WIDTH + SCENE_PROMPT_SLOT_WIDTH / 2)

// Popups and modals: one-row prompt area relative to bottom of their rectangle.
// Place prompt row at (rect bottom) - POPUP_PROMPT_ROW_OFFSET. One-row two-slot layout.
#define POPUP_ROUNDRECT_RADIUS 18
#define POPUP_PROMPT_ROW_OFFSET 20
#define POPUP_PROMPT_SLOT_CENTER_X(vx1, vx2, slotIndex) ((vx1) + ((2 * (slotIndex) + 1) * ((vx2) - (vx1))) / 4)
#define POPUP_PROMPT_ROW_Y(vy2) ((vy2) - POPUP_PROMPT_ROW_OFFSET)

#endif
