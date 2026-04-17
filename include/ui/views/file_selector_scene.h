#ifndef _UI_VIEWS_FILE_SELECTOR_SCENE_H_
#define _UI_VIEWS_FILE_SELECTOR_SCENE_H_

struct View;

typedef void (*FileSelectorDoneFn)(const char *path, void *userdata);
typedef void (*FileSelectorDismissFn)(void *userdata);

// Optional: invoked when the selector is popped without onDone (cancel) or after onDone (idempotent clears are OK).
void fileSelectorSetDismissNotifier(FileSelectorDismissFn fn, void *userdata);

// Configure selector before pushing its view on stack.
void fileSelectorConfigure(const char *initialPath, FileSelectorDoneFn onDone, void *userdata);

// Browse only under mountRoot (e.g. title device). Circle at mount root cancels (Pop) without selecting.
void fileSelectorConfigureForMount(const char *mountRoot, FileSelectorDoneFn onDone, void *userdata);

// Modal file selector view.
struct View *fileSelectorGetView(void);

#endif
