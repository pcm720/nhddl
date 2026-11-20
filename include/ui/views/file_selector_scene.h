#ifndef _UI_VIEWS_FILE_SELECTOR_SCENE_H_
#define _UI_VIEWS_FILE_SELECTOR_SCENE_H_

struct View;

typedef void (*FileSelectorDoneFn)(const char *path, void *userdata);
typedef void (*FileSelectorDismissFn)(void *userdata);

// Optional: invoked when the selector is popped without onDone (cancel) or after onDone (idempotent clears are OK).
void fileSelectorSetDismissNotifier(FileSelectorDismissFn fn, void *userdata);

// Configure selector before pushing its view on stack.
// heading: optional modal title; NULL uses "Select device"/"Select file".
void fileSelectorConfigure(const char *initialPath, FileSelectorDoneFn onDone, void *userdata, const char *heading);

// Browse only under mountRoot (e.g. title device). Circle at mount root cancels (Pop) without selecting.
// heading: optional modal title; NULL uses "Select file".
void fileSelectorConfigureForMount(const char *mountRoot, FileSelectorDoneFn onDone, void *userdata, const char *heading);

// After configure: device list = "Select Neutrino device", browse = "Select Neutrino ELF".
void fileSelectorEnableNeutrinoHeadings(void);

// Modal file selector view.
struct View *fileSelectorGetView(void);

#endif
