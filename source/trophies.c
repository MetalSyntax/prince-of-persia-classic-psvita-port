/*
 * PS Vita native trophy implementation for Prince of Persia Classic.
 * Bridges game achievement triggers to sceNpTrophy via NoTrpDrm.
 *
 * Implementation notes (Rinnegatamante / NoTrpDrm style):
 * - NoTrpDrm disables the NP communication signature check, so the same
 *   placeholder signature bytes Rinnegatamante ships are enough; comm_id is
 *   the bare 9-char base ("POPC00001", TRP <npcommid> is "POPC00001_00").
 * - A single persistent trophy handle is created at init and reused for
 *   every unlock, guarded by a mutex (creating/destroying a handle per
 *   unlock is fragile and hides SETUP_REQUIRED errors).
 * - The unlock cache bit is set ONLY after sceNpTrophyUnlockTrophy reports
 *   success (or ALREADY_UNLOCKED). Setting it before the call is what made
 *   the game believe a trophy was unlocked while the OS never showed the
 *   notification and later retries were skipped.
 * - Platinum is NEVER unlocked manually: the OS auto-unlocks it when the
 *   last base trophy is earned and returns its id via the plat_id out-param.
 *   Manually calling UnlockTrophy(platinum) returns
 *   0x80551610 (PLATINUM_CANNOT_UNLOCK) and no notification.
 */

#include <vitasdk.h>
#include <vitaGL.h>
#include <stdio.h>
#include <string.h>

#include "trophies.h"
#include "utils/logger.h"

/* ------------------------------------------------------------------ */
/* Vita NpTrophy bindings (not shipped in vitasdk headers, resolved   */
/* by NID at link time via SceNpTrophy_stub).                         */
/* ------------------------------------------------------------------ */

typedef int SceNpTrophyContext;
typedef int SceNpTrophyHandle;
typedef int SceNpTrophyId;

typedef struct {
    int sdkVersion;
    SceCommonDialogParam commonParam;
    SceNpTrophyContext context;
    int options;
    uint8_t reserved[128];
} SceNpTrophySetupDialogParam;

/* 128 trophies max -> 4 x 32-bit flag words (see SceNpTrophy spec). */
typedef struct {
    uint32_t bits[4];
} SceNpTrophyFlagArray;

/* Game-info structs for diagnostics (size field must be set, the OS
 * validates details->size == sizeof(...)). */
#define SCE_NP_TROPHY_TITLE_MAX_SIZE 128
#define SCE_NP_TROPHY_DESCR_MAX_SIZE 1024
typedef struct {
    SceSize size;
    uint32_t numGroups;
    uint32_t numTrophies;
    uint32_t numPlatinum;
    uint32_t numGold;
    uint32_t numSilver;
    uint32_t numBronze;
    char title[SCE_NP_TROPHY_TITLE_MAX_SIZE];
    char description[SCE_NP_TROPHY_DESCR_MAX_SIZE];
} SceNpTrophyGameDetails;

typedef struct {
    SceSize size;
    uint32_t unlockedTrophies;
    uint32_t unlockedPlatinum;
    uint32_t unlockedGold;
    uint32_t unlockedSilver;
    uint32_t unlockedBronze;
    uint32_t progressPercentage;
} SceNpTrophyGameData;

int sceNpTrophyInit(void *unk);
int sceNpTrophyCreateContext(SceNpTrophyContext *context, const char *commId,
                             const void *commSign, uint64_t options);
int sceNpTrophySetupDialogInit(SceNpTrophySetupDialogParam *param);
SceCommonDialogStatus sceNpTrophySetupDialogGetStatus(void);
int sceNpTrophySetupDialogTerm(void);
int sceNpTrophyCreateHandle(SceNpTrophyHandle *handle);
int sceNpTrophyDestroyHandle(SceNpTrophyHandle handle);
int sceNpTrophyUnlockTrophy(SceNpTrophyContext ctx, SceNpTrophyHandle handle,
                            SceNpTrophyId id, SceNpTrophyId *plat_id);
int sceNpTrophyGetTrophyUnlockState(SceNpTrophyContext ctx, SceNpTrophyHandle handle,
                                    SceNpTrophyFlagArray *flags, uint32_t *count);
int sceNpTrophyGetGameInfo(SceNpTrophyContext ctx, SceNpTrophyHandle handle,
                           SceNpTrophyGameDetails *details, SceNpTrophyGameData *data);

/* Known error codes (firmware 3.60 SceNpTrophy). */
#define SCE_NP_TROPHY_ERROR_NOT_INITIALIZED        ((int)0x80551601)
#define SCE_NP_TROPHY_ERROR_INVALID_HANDLE         ((int)0x80551608)
#define SCE_NP_TROPHY_ERROR_INVALID_CONTEXT        ((int)0x80551609)
#define SCE_NP_TROPHY_ERROR_INVALID_TROPHY_ID      ((int)0x8055160E)
#define SCE_NP_TROPHY_ERROR_TROPHY_ALREADY_UNLOCKED ((int)0x8055160F)
#define SCE_NP_TROPHY_ERROR_PLATINUM_CANNOT_UNLOCK  ((int)0x80551610)
#define SCE_NP_TROPHY_ERROR_SETUP_REQUIRED         ((int)0x80551612)
#define SCE_NP_TROPHY_ERROR_TRP_FILE_NOT_FOUND     ((int)0x80551619)

static const char *trp_err_str(int res) {
    switch (res) {
        case 0: return "OK";
        case SCE_NP_TROPHY_ERROR_NOT_INITIALIZED: return "NOT_INITIALIZED";
        case SCE_NP_TROPHY_ERROR_INVALID_HANDLE: return "INVALID_HANDLE";
        case SCE_NP_TROPHY_ERROR_INVALID_CONTEXT: return "INVALID_CONTEXT";
        case SCE_NP_TROPHY_ERROR_INVALID_TROPHY_ID: return "INVALID_TROPHY_ID";
        case SCE_NP_TROPHY_ERROR_TROPHY_ALREADY_UNLOCKED: return "ALREADY_UNLOCKED";
        case SCE_NP_TROPHY_ERROR_PLATINUM_CANNOT_UNLOCK: return "PLATINUM_CANNOT_UNLOCK";
        case SCE_NP_TROPHY_ERROR_SETUP_REQUIRED: return "SETUP_REQUIRED (TRP/dialog)";
        case SCE_NP_TROPHY_ERROR_TRP_FILE_NOT_FOUND: return "TRP_FILE_NOT_FOUND";
        default: return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

#define TRP_COMM_ID_DEFAULT "POPC00001"
#define TRP_MAX_TROPHIES 128

/* NOTE: bare 9-char base like Rinnegatamante ("ELDR00001"), NOT the "_00"
 * suffixed form. param.sfo intentionally carries no NP_COMMUNICATION_ID
 * (see CMakeLists.txt), so the sfo lookup below always falls back here. */
static char trp_comm_id[16] = TRP_COMM_ID_DEFAULT;
/* Same placeholder signature bytes Rinnegatamante ships (NoTrpDrm skips the
 * verification, but keep the proven values instead of inventing new ones). */
static uint8_t trp_comm_sign[160] = {0xb9, 0xdd, 0xe1, 0x3b, 0x01, 0x00};

static SceNpTrophyContext trp_ctx = -1;
static SceNpTrophyHandle trp_handle = -1;
static SceNpTrophyFlagArray trp_flags;
static uint32_t trp_count = 0;
static int trophies_available = 0;

#define TRP_QUEUE_SIZE 32
static uint32_t trp_queue[TRP_QUEUE_SIZE];
static int trp_queue_head = 0;
static int trp_queue_tail = 0;
static SceUID trp_queue_mutex = -1;
static SceUID trp_request_sema = -1;
static SceUID trp_api_mutex = -1; /* guards trp_handle + trp_flags */

static void trp_flag_set(uint32_t id) {
    trp_flags.bits[id >> 5] |= (uint32_t)(1u << (id & 31u));
}

static int trp_flag_get(uint32_t id) {
    return (trp_flags.bits[id >> 5] & (uint32_t)(1u << (id & 31u))) != 0;
}

/* Runs on a worker thread so the render loop never blocks on trophy IO.
 * Uses the single persistent handle; marks cache only on real success. */
static int trophies_unlocker_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;

    while (1) {
        sceKernelWaitSema(trp_request_sema, 1, NULL);

        uint32_t id = 0;
        sceKernelWaitSema(trp_queue_mutex, 1, NULL);
        if (trp_queue_head != trp_queue_tail) {
            id = trp_queue[trp_queue_tail];
            trp_queue_tail = (trp_queue_tail + 1) % TRP_QUEUE_SIZE;
        } else {
            sceKernelSignalSema(trp_queue_mutex, 1);
            continue;
        }
        sceKernelSignalSema(trp_queue_mutex, 1);

        sceKernelWaitSema(trp_api_mutex, 1, NULL);
        SceNpTrophyId plat_id = -1;
        int res = sceNpTrophyUnlockTrophy(trp_ctx, trp_handle, (SceNpTrophyId)id, &plat_id);
        if (res == 0) {
            trp_flag_set(id);
            l_info("Trophy %u unlocked (notificacion del sistema emitida por Vita OS)", (unsigned)id);
            if (plat_id >= 0) {
                trp_flag_set((uint32_t)plat_id);
                l_success("Platinum auto-unlocked by the system (id=%d)", plat_id);
            }
        } else if (res == SCE_NP_TROPHY_ERROR_TROPHY_ALREADY_UNLOCKED) {
            trp_flag_set(id);
            l_info("Trophy %u already unlocked on Vita OS side (cache sincronizado)", (unsigned)id);
        } else {
            /* Do NOT set the cache bit: the OS did not unlock anything,
             * so a later retry (or the Trophies app state) stays coherent. */
            l_warn("sceNpTrophyUnlockTrophy(id=%u) failed: 0x%08X (%s). "
                   "Revisa NoTrpDrm en ur0:tai/config.txt (*main) y que el TRP este instalado.",
                   (unsigned)id, (unsigned)res, trp_err_str(res));
        }
        sceKernelSignalSema(trp_api_mutex, 1);
    }
    return 0;
}

int trophies_init(void) {
    memset(&trp_flags, 0, sizeof(trp_flags));

    char app_comm_id[256];
    int res_param = sceAppMgrAppParamGetString(0, 12, app_comm_id, sizeof(app_comm_id));
    l_debug("app param(12) readback: res=%d value='%s'",
            res_param, (res_param >= 0 && app_comm_id[0]) ? app_comm_id : "(empty)");
    if (res_param >= 0 && app_comm_id[0] != '\0') {
        /* Keep only the bare 9-char base ("ELDR00001" style): strip any "_00". */
        char *sep = strchr(app_comm_id, '_');
        if (sep) *sep = '\0';
        strncpy(trp_comm_id, app_comm_id, sizeof(trp_comm_id) - 1);
        trp_comm_id[sizeof(trp_comm_id) - 1] = '\0';
    } else {
        snprintf(trp_comm_id, sizeof(trp_comm_id), "%s", TRP_COMM_ID_DEFAULT);
    }

    l_info("Initializing trophies with comm_id: %s", trp_comm_id);

    /* Diagnostic: is our TRP actually visible from app0:? If the VPK install
     * dropped sce_sys/trophy (or the folder name mismatches), the setup
     * dialog can never install anything -- rule this out before blaming
     * signatures/plugins. */
    {
        char trp_path[128];
        snprintf(trp_path, sizeof(trp_path), "app0:sce_sys/trophy/%s_00/TROPHY.TRP", trp_comm_id);
        SceUID trp_fd = sceIoOpen(trp_path, SCE_O_RDONLY, 0);
        if (trp_fd >= 0) {
            SceOff trp_sz = sceIoLseek(trp_fd, 0, SCE_SEEK_END);
            sceIoClose(trp_fd);
            l_info("TRP readable at %s (%lld bytes)", trp_path, (long long)trp_sz);
        } else {
            l_warn("TRP NOT readable at %s (0x%08X) -- VPK sce_sys/trophy missing?",
                   trp_path, (unsigned)trp_fd);
        }
    }

    int res_sys = sceSysmoduleLoadModule(SCE_SYSMODULE_NP_TROPHY);
    l_debug("sceSysmoduleLoadModule(NP_TROPHY) = 0x%08X", (unsigned)res_sys);

    int res_init = sceNpTrophyInit(NULL);
    if (res_init < 0 && res_init != (int)0x80551602 /* ALREADY_INITIALIZED */) {
        l_warn("sceNpTrophyInit failed: 0x%08X", (unsigned)res_init);
        return res_init;
    }

    int res = sceNpTrophyCreateContext(&trp_ctx, trp_comm_id, trp_comm_sign, 0);
    if (res < 0) {
        l_warn("sceNpTrophyCreateContext(%s) failed: 0x%08X (%s). "
               "Ensure NoTrpDrm plugin is installed in ur0:tai/config.txt (*main) "
               "and NP_COMMUNICATION_ID matches the TRP <npcommid>.",
               trp_comm_id, (unsigned)res, trp_err_str(res));
        return res;
    }
    l_debug("sceNpTrophyCreateContext OK (ctx=%d)", trp_ctx);

    /* The setup dialog installs/registers the TRP data on first run.
     * It needs a running display: main() calls trophies_init() after gl_init(). */
    SceNpTrophySetupDialogParam setupParam;
    memset(&setupParam, 0, sizeof(setupParam));
    _sceCommonDialogSetMagicNumber(&setupParam.commonParam);
    setupParam.sdkVersion = PSP2_SDK_VERSION;
    setupParam.context = trp_ctx;
    setupParam.options = 0;

    int res_dlg = sceNpTrophySetupDialogInit(&setupParam);
    if (res_dlg >= 0) {
        while (sceNpTrophySetupDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_RUNNING) {
            sceDisplayWaitVblankStart();
            vglSwapBuffers(GL_TRUE);
        }
        sceNpTrophySetupDialogTerm();
        l_debug("Trophy setup dialog finished");
    } else {
        l_warn("sceNpTrophySetupDialogInit returned 0x%08X (%s)",
               (unsigned)res_dlg, trp_err_str(res_dlg));
    }

    /* Single persistent handle reused by every unlock (Rinnegatamante style). */
    int res_h = sceNpTrophyCreateHandle(&trp_handle);
    if (res_h < 0) {
        l_warn("sceNpTrophyCreateHandle failed: 0x%08X (%s)", (unsigned)res_h, trp_err_str(res_h));
        return res_h;
    }

    uint32_t count = 0;
    int res_st = sceNpTrophyGetTrophyUnlockState(trp_ctx, trp_handle, &trp_flags, &count);
    if (res_st < 0) {
        l_warn("sceNpTrophyGetTrophyUnlockState failed: 0x%08X (%s). "
               "Trophies disabled; check TRP install + NoTrpDrm.",
               (unsigned)res_st, trp_err_str(res_st));
        sceNpTrophyDestroyHandle(trp_handle);
        trp_handle = -1;
        return res_st;
    }
    if (count == 0 || count > TRP_MAX_TROPHIES) {
        l_warn("sceNpTrophyGetTrophyUnlockState returned bogus count=%u", (unsigned)count);
        sceNpTrophyDestroyHandle(trp_handle);
        trp_handle = -1;
        return -1;
    }
    trp_count = count;
    l_info("Trophy state loaded: count=%u mask0=0x%08X", (unsigned)trp_count, (unsigned)trp_flags.bits[0]);

    /* Diagnostic: totals from the TRP (also proves the handle works). */
    SceNpTrophyGameDetails details;
    SceNpTrophyGameData data;
    memset(&details, 0, sizeof(details));
    memset(&data, 0, sizeof(data));
    details.size = sizeof(details);
    data.size = sizeof(data);
    if (sceNpTrophyGetGameInfo(trp_ctx, trp_handle, &details, &data) == 0) {
        l_info("TRP game info: trophies=%u (P=%u G=%u S=%u B=%u) unlocked=%u (%u%%)",
               (unsigned)details.numTrophies, (unsigned)details.numPlatinum,
               (unsigned)details.numGold, (unsigned)details.numSilver,
               (unsigned)details.numBronze, (unsigned)data.unlockedTrophies,
               (unsigned)data.progressPercentage);
    }

    trp_queue_mutex = sceKernelCreateSema("trp_queue_mutex", 0, 1, 1, NULL);
    trp_request_sema = sceKernelCreateSema("trp_request_sema", 0, 0, TRP_QUEUE_SIZE, NULL);
    trp_api_mutex = sceKernelCreateSema("trp_api_mutex", 0, 1, 1, NULL);
    if (trp_queue_mutex < 0 || trp_request_sema < 0 || trp_api_mutex < 0) {
        l_warn("trophy sync primitives creation failed");
        return -1;
    }

    SceUID thid = sceKernelCreateThread("trophies_unlocker", trophies_unlocker_thread,
                                        0x10000100, 0x10000, 0, 0, NULL);
    if (thid < 0) {
        l_warn("sceKernelCreateThread(trophies_unlocker) failed: 0x%08X", (unsigned)thid);
        return (int)thid;
    }
    if (sceKernelStartThread(thid, 0, NULL) < 0) {
        l_warn("sceKernelStartThread(trophies_unlocker) failed");
        sceKernelDeleteThread(thid);
        return -1;
    }

    trophies_available = 1;
    l_success("PS Vita Trophy subsystem initialized (%u trophies).", (unsigned)trp_count);
    return 0;
}

uint8_t trophies_is_unlocked(uint32_t id) {
    if (!trophies_available) {
        return 0;
    }
    if (id >= trp_count) {
        return 0;
    }
    sceKernelWaitSema(trp_api_mutex, 1, NULL);
    int v = trp_flag_get(id);
    sceKernelSignalSema(trp_api_mutex, 1);
    return (uint8_t)v;
}

void trophies_unlock(uint32_t id) {
    if (!trophies_available) {
        l_warn("trophies_unlock(%u) called but trophies not available", (unsigned)id);
        return;
    }
    if (id >= trp_count) {
        l_warn("trophies_unlock(%u) out of range (count=%u)", (unsigned)id, (unsigned)trp_count);
        return;
    }
    if (id == 0) {
        /* Platinum must come from the OS auto-unlock path (plat_id out-param).
         * Manual requests are rejected by the OS with PLATINUM_CANNOT_UNLOCK,
         * so drop them here with a clear message instead of spamming errors. */
        l_debug("trophies_unlock(0/Platinum) ignored: el platino lo desbloquea solo Vita OS");
        return;
    }

    /* NOTE: the cache bit is intentionally NOT set here. It is set by the
     * worker thread only after sceNpTrophyUnlockTrophy succeeds, so the
     * in-game state can never diverge from what Vita OS (and its trophy
     * notification) actually registered. */
    sceKernelWaitSema(trp_queue_mutex, 1, NULL);
    int next_head = (trp_queue_head + 1) % TRP_QUEUE_SIZE;
    if (next_head != trp_queue_tail) {
        trp_queue[trp_queue_head] = id;
        trp_queue_head = next_head;
        sceKernelSignalSema(trp_request_sema, 1);
        l_info("Queued trophy unlock for ID %u", (unsigned)id);
    } else {
        l_warn("Trophy queue full, dropping unlock for ID %u", (unsigned)id);
    }
    sceKernelSignalSema(trp_queue_mutex, 1);
}
