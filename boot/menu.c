#include <stdint.h>            // for uint32_t, uint8_t
#include <stdlib.h>            // for NULL
#include <string.h>            // for memcmp
#include <ctr9/ctr_system.h>   // for ctr_system_poweroff, ctr_system_reset
#include <malloc.h>            // for memalign
#include <menu-backend.h>      // for type::unselectable, type::option, opti...
#include <option.h>            // for get_opt, toggle_opt, save_config, OPTI...
#include <firm/headers.h>
#include <patcher.h>           // for generate_patch_cache
#include <std/draw.h>          // for crflush, BOTTOM_SCREEN, stderr
#include <std/fs.h>            // for crumount, read_file, recurse_call
#include <std/memory.h>        // for strdup_self
#include <structures.h>        // for system_patch, PATH_AUX_PATCHES, PATH_L...
#include "config.h"            // for LOCALSTATEDIR
#include "ctr9/io/fatfs/ff.h"  // for FILINFO, f_stat, ::FR_OK, AM_DIR

// FIXME - Remove limit
#define MAX_PATCHES 256
struct options_s *patches = NULL;
uint8_t *enable_list;

#define MAX_FIRMS 50
struct options_s *firm = NULL;

static int current_menu_index_patches = 0;

#if defined(CHAINLOADER) && CHAINLOADER == 1
void chainload_menu();
#endif

#ifndef REL
#define REL "master"
#endif

#define ln(s) { s, "", unselectable, 0, NULL, NULL, 0, 0 }
#define lnh(s) { s, "", unselectable, 0, NULL, NULL, 0, 1 }

void config_main_menu();
void poweroff();
void reset();

static struct options_s options[] = {
    // Patches.
    { "General Options", "", unselectable, 0, NULL, NULL, 0, 1 },

    { "System Module Inject",
      "Replaces system modules in FIRM like loader, fs, pxi, etc.",
      option, (void*)OPTION_LOADER, toggle_opt, get_opt, 0, 0 },

    { "svcBackdoor Fixup",
      "Reinserts svcBackdoor on 11.0 NATIVE_FIRM. svcBackdoor allows executing arbitrary functions with ARM11 kernel permissions, and is required by some (poorly coded) applications.",
      option, (void*)OPTION_SVCS, toggle_opt, get_opt, 0, 0 },

    { "Firmlaunch Hook",
      "Hooks firmlaunch to allow largemem games on o3DS. Also allows patching TWL/AGB on all consoles. Previously called 'Reboot hook' but renamed for accuracy.",
      option, (void*)OPTION_REBOOT, toggle_opt, get_opt, 0, 0 },

    { "Use EmuNAND",
      "Redirects NAND write/read to the SD. This supports both Gateway and redirected layouts.",
      option, (void*)OPTION_EMUNAND, toggle_opt, get_opt, 0, 0 },
    { "Index",
      "Which EmuNAND to use. If you only have one, you want 0. Currently the maximum supported is 10 (0-9), but this is arbitrary.",
      option, (void*)OPTION_EMUNAND_INDEX, toggle_opt, get_opt, 1, 0 },

    { "Autoboot",
      "Boot the system automatically, unless the R key is held while booting.",
      option, (void*)OPTION_AUTOBOOT, toggle_opt, get_opt, 0, 0 },
    { "Silent mode",
      "Suppress all debug output during autoboot. If backgrounds are present, they will be shown briefly, otherwise the screen will remain off.",
      option, (void*)OPTION_SILENCE, toggle_opt, get_opt, 1, 0 },

    { "Dim Background",
      "Experimental! Dims colors on lighter backgrounds to improve readability with text. You won't notice the change until scrolling or exiting the current menu due to the way rendering works.",
      option, (void*)OPTION_DIM_MODE, toggle_opt, get_opt, 0, 0 },

    { "Accent color",
      "Changes the accent color in menus.",
      option, (void*)OPTION_ACCENT_COLOR, toggle_opt, get_opt, 0, 0 },

    { "Brightness",
      "Changes the screeninit brightness in menu. WIP, only takes effect on reboot (this will change.)",
      option, (void*)OPTION_BRIGHTNESS, toggle_opt, get_opt, 0, 0 },

    // space
    { "", "", unselectable, 0, NULL, NULL, 0, 0 },
    // Patches.
    { "Loader Options", "", unselectable, 0, NULL, NULL, 0, 1 },

    { "CPU - L2 cache",
      "Forces the system to use the L2 cache on all applications. If you have issues with crashes, try turning this off.",
      option_n3ds, (void*)OPTION_LOADER_CPU_L2, toggle_opt, get_opt, 0, 0 },
    { "CPU - 804Mhz",
      "Forces the system to run in 804Mhz mode on all applications.",
      option_n3ds, (void*)OPTION_LOADER_CPU_800MHZ, toggle_opt, get_opt, 0, 0 },
    { "Language Emulation",
      "Reads language emulation configuration from `" PATH_LOCEMU "` and imitates the region/language.",
      option, (void*)OPTION_LOADER_LANGEMU, toggle_opt, get_opt, 0, 0 },
    { "Load Code Sections",
      "Loads code sections (text/ro/data) from SD card and patches afterwards.",
      option, (void*)OPTION_LOADER_LOADCODE, toggle_opt, get_opt, 0, 0 },
    { "Dump Code Sections",
      "Dumps code sections for titles to SD card the first time they're loaded. Slows things down on first launch.",
      option, (void*)OPTION_LOADER_DUMPCODE, toggle_opt, get_opt, 0, 0 },
    { "Merged codebins",
      "Use merged codebins rather than segment split files.",
      option, (void*)OPTION_LOADER_DUMPCODE_MERGED, toggle_opt, get_opt, 1, 0 },
    { "+ System Titles",
      "Dumps code sections for system titles, too. Expect to sit at a blank screen for >3mins on the first time you do this, because it dumps everything.",
      option, (void*)OPTION_LOADER_DUMPCODE_ALL, toggle_opt, get_opt, 1, 0 },

    // space
    { "", "", unselectable, 0, NULL, NULL, 0, 0 },
    // Patches.
    { "Developer Options", "", unselectable, 0, NULL, NULL, 0, 1 },

    { "Step Through",
      "After each important step, [WAIT] will be shown and you'll need to press a key. Debug feature.",
      option, (void*)OPTION_TRACE, toggle_opt, get_opt, 0, 0 },
    { "Verbose",
      "Output more debug information than the average user needs.",
      option, (void*)OPTION_OVERLY_VERBOSE, toggle_opt, get_opt, 0, 0 },
    { "Logging",
      "Save logs to `" LOCALSTATEDIR "` as `boot.log` and `loader.log`. Slows operation a bit.",
      option, (void*)OPTION_SAVE_LOGS, toggle_opt, get_opt, 0, 0 },

    // Sentinel.
    { NULL, NULL, 0, 0, NULL, NULL, 0, 0 }, // cursor_min and cursor_max are stored in the last two.
};

static struct options_s config_opts[] = {
    { "Options",
      "Internal options for the CFW.\nThese are part of " FW_NAME " itself.",
      option, options, (void(*)(void*))show_menu, NULL, 0, 0 },
    { "Patches",
      "External bytecode patches found in `" PATH_PATCHES "` and `" PATH_AUX_PATCHES "` .\nYou can choose which to enable.",
      option, NULL, (void(*)(void*))show_menu, NULL, 0, 0 },

    // Sentinel.
    { NULL, NULL, 0, 0, NULL, NULL, 0, 0 }, // cursor_min and cursor_max are stored in the last two.
};

static struct options_s help_d[] = {
    lnh("About"),
    ln("  This is another 3DS CFW for power users."),
    ln("  It seeks to address some faults in other"),
    ln("  CFWs and is generally just another choice"),
    ln("  for users - but primarily is intended for"),
    ln("  developers. It is not for the faint of heart."),
    ln(""),
    lnh("Usage"),
    ln("  A         -> Select/Toggle/Increment"),
    ln("  B         -> Back/Boot"),
    ln("  X         -> Decrement"),
    ln("  Select    -> Help/Information"),
    ln("  Down      -> Down"),
    ln("  Right     -> Down five"),
    ln("  Up        -> Up"),
    ln("  Left      -> Up five"),
    ln("  L+R+Start -> Menu Screenshot"),
    ln(""),
    lnh("Credits"),
    ln("  @mid-kid, @Wolfvak, @Reisyukaku, @AuroraWright"),
    ln("  @d0k3, @TuxSH, @Steveice10, @delebile,"),
    ln("  @Normmatt, @b1l1s, @dark-samus, @TiniVi,"),
    ln("  @gemarcano, and anyone else I may have"),
    ln("  forgotten (yell at me, please!)"),
    ln(""),
    lnh("  <https://github.com/chaoskagami/corbenik>"),
    { NULL, NULL, unselectable, 0, NULL, NULL, 0, 0 }, // cursor_min and cursor_max are stored in the last two.
};


#define quote(x) #x
#define string(x) quote(x)

static struct options_s build_d[] = {
    lnh("Build Info"),
    ln ("  Commit hash"),
    ln ("    " string(REVISION) " (" string(BRANCH) ")"),
    ln ("  Compiler"),
    ln ("    " string(COMPILER_ID) ),
    ln ("  Build configuration" ),
    ln ("    " string(CONFIGURE_OPTIONS) ),
    { NULL, NULL, unselectable, 0, NULL, NULL, 0, 0 }, // cursor_min and cursor_max are stored in the last two.
};

static struct options_s main_s[] = {
    { "Configuration",
      "Configuration options for the CFW.",
      option, 0, config_main_menu, NULL, 0, 0 },
    { "Readme",
      "Mini-readme.\nWhy are you opening help on this, though?\nThat's kind of silly.",
      option, help_d, (void(*)(void*))show_menu, NULL, 0, 0 },
    { "Build Info",
      "Information such as GCC version for build, commit hash, flags, etc",
      option, build_d, (void(*)(void*))show_menu, NULL, 0, 0 },
    { "Reboot",
      "Reboots the console.",
      option, 0, reset, NULL, 0, 0 },
    { "Power off",
      "Powers off the console.",
      option, 0, poweroff, NULL, 0, 0 },
#if defined(CHAINLOADER) && CHAINLOADER == 1
    { "Chainload",
      "Boot another ARM9 payload file.",
       option, 0, chainload_menu, NULL, 0, 0 },
#endif
    { "Boot Firmware",
      "Patches the firmware, and boots it.",
      break_menu, 0, NULL, NULL, 0, 0 },

    // Sentinel.
    { NULL, NULL, 0, 0, NULL, NULL, 0, 0 }, // cursor_min and cursor_max are stored in the last two.
};

//===================================================================================================

char* patch_get_enable(void* val) {
    uint32_t opt = (uint32_t)val;
    uint32_t raw = enable_list[opt];
    static char ret[2] = " ";
    ret[0] = ' ';
    if (raw == 1)
        ret[0] = '*';
    return ret;
}

void patch_set_enable(void* val) {
    uint32_t opt = (uint32_t)val;
    enable_list[opt] = !enable_list[opt];
}

void patch_func(char* fpath) {
    FILINFO f2;
    if (f_stat(fpath, &f2) != FR_OK)
        return;

    if (!(f2.fattrib & AM_DIR)) {
        struct system_patch p;
        read_file(&p, fpath, sizeof(struct system_patch));

        if (memcmp(p.magic, "AIDA", 4))
            return;

        patches[current_menu_index_patches].name = strdup_self(p.name);
        patches[current_menu_index_patches].desc = strdup_self(p.desc);

        uint32_t val = p.uuid;

        patches[current_menu_index_patches].param = (void*)val;

        patches[current_menu_index_patches].handle = option;
        patches[current_menu_index_patches].indent = 0;
        patches[current_menu_index_patches].highlight = 0;

        patches[current_menu_index_patches].func  = patch_set_enable;
        patches[current_menu_index_patches].value = patch_get_enable;

        current_menu_index_patches++;
    }
}

//===================================================================================================

// TODO - Enumerate FIRMs and list

//===================================================================================================

void
reset()
{
    crflush(stderr);

    crumount(); // Unmount SD.

    set_opt_u32(OPTION_SAVE_LOGS, 0); // FS unmounted, can't log anymore

    // Reboot.
    fprintf(BOTTOM_SCREEN, "Rebooting system...\n");

    ctr_system_reset();
}

void
poweroff()
{
    crflush(stderr);

    crumount(); // Unmount SD.

    set_opt_u32(OPTION_SAVE_LOGS, 0); // FS unmounted, can't log anymore

    // Power off
    fprintf(BOTTOM_SCREEN, "Powering off system...\n");

    ctr_system_poweroff();
}

void
reset_patch_menu(void)
{
    current_menu_index_patches = 0;

    if (!patches)
        patches = memalign(16, sizeof(struct options_s) * 258); // FIXME - hard limit. Implement realloc.

    patches[0].name   = "Patches";
    patches[0].desc   = "";
    patches[0].handle = unselectable;
    patches[0].indent = 0;
    patches[0].func   = NULL;
    patches[0].value  = NULL;
    patches[0].highlight = 1;

    current_menu_index_patches += 1;
}

void
add_patch_menu(const char *name)
{
    recurse_call(name, patch_func);

    patches[current_menu_index_patches].name = NULL;

    config_opts[1].param = (void*)patches;
}

void config_main_menu() {
    show_menu(config_opts);

    save_config(); // Save config when exiting.

    generate_patch_cache();
}

void
menu_handler(void)
{
    show_menu(main_s);
}
