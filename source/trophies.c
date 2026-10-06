/*
 * PS Vita native trophy implementation for Prince of Persia Classic.
 * Bridges game achievement triggers to sceNpTrophy via NoTrpDrm.
 */

#include <vitasdk.h>
#include <stdio.h>
#include <string.h>

#include "trophies.h"
#include "utils/logger.h"

static char comm_id[16] = "POPC00001_00";
static char signature[160] = {0xb9, 0xdd, 0xe1, 0x3b, 0x01, 0x00};

static int trp_ctx = 0;
static int plat_id = 0; // ID 0 is Platinum
static int trophies_available = 0;

typedef struct {
    int sdkVersion;
    SceCommonDialogParam commonParam;
    int context;
    int options;
    uint8_t reserved[128];
} SceNpTrophySetupDialogParam;

typedef struct {
    uint32_t unk[4];
} SceNpTrophyUnlockState;

static SceNpTrophyUnlockState trophies_unlocks;

#define TRP_QUEUE_SIZE 32
static uint32_t trp_queue[TRP_QUEUE_SIZE];
static int trp_queue_head = 0;
static int trp_queue_tail = 0;
static SceUID trp_queue_mutex = -1;
static SceUID trp_request_sema = -1;

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
        }
        sceKernelSignalSema(trp_queue_mutex, 1);

        int trp_handle = 0;
        int res_h = sceNpTrophyCreateHandle(&trp_handle);
        if (res_h >= 0) {
            int res = sceNpTrophyUnlockTrophy(trp_ctx, trp_handle, (int)id, &plat_id);
            l_info("sceNpTrophyUnlockTrophy(id=%u) returned 0x%08X", (unsigned)id, (unsigned)res);
            sceNpTrophyDestroyHandle(trp_handle);
        } else {
            l_warn("sceNpTrophyCreateHandle failed: 0x%08X", (unsigned)res_h);
        }

        // Check if all 17 base trophies (1..17) are unlocked; if so, trigger Platinum (0)
        if (id != 0 && !trophies_is_unlocked(0)) {
            int all_done = 1;
            for (uint32_t i = 1; i <= 17; i++) {
                if (!trophies_is_unlocked(i)) {
                    all_done = 0;
                    break;
                }
            }
            if (all_done) {
                l_info("All 17 trophies unlocked! Unlocking Platinum (ID 0)...");
                trophies_unlock(0);
            }
        }
    }
    return 0;
}

int trophies_init(void) {
    char app_comm_id[256];
    if (sceAppMgrAppParamGetString(0, 12, app_comm_id, sizeof(app_comm_id)) >= 0 && app_comm_id[0] != '\0') {
        strncpy(comm_id, app_comm_id, sizeof(comm_id) - 1);
        comm_id[sizeof(comm_id) - 1] = '\0';
    }

    l_info("Initializing trophies with comm_id: %s", comm_id);

    int res_sys = sceSysmoduleLoadModule(SCE_SYSMODULE_NP_TROPHY);
    l_debug("sceSysmoduleLoadModule(NP_TROPHY) = 0x%08X", (unsigned)res_sys);

    sceNpTrophyInit(NULL);

    int res = sceNpTrophyCreateContext(&trp_ctx, comm_id, signature, 0);
    if (res < 0) {
        l_warn("sceNpTrophyCreateContext failed (0x%08X). Ensure NoTrpDrm plugin is installed.", (unsigned)res);
        return res;
    }

    SceNpTrophySetupDialogParam setupParam;
    memset(&setupParam, 0, sizeof(setupParam));
    _sceCommonDialogSetMagicNumber(&setupParam.commonParam);
    setupParam.sdkVersion = PSP2_SDK_VERSION;
    setupParam.context = trp_ctx;
    setupParam.options = 0;

    int res_dlg = sceNpTrophySetupDialogInit(&setupParam);
    if (res_dlg >= 0) {
        while (sceNpTrophySetupDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_RUNNING) {
            sceKernelDelayThread(16000);
        }
        sceNpTrophySetupDialogTerm();
    } else {
        l_warn("sceNpTrophySetupDialogInit returned 0x%08X", (unsigned)res_dlg);
    }

    trp_queue_mutex = sceKernelCreateSema("trp_queue_mutex", 0, 1, 1, NULL);
    trp_request_sema = sceKernelCreateSema("trp_request_sema", 0, 0, TRP_QUEUE_SIZE, NULL);

    SceUID thid = sceKernelCreateThread("trophies_unlocker", trophies_unlocker_thread, 0x10000100, 0x10000, 0, 0, NULL);
    if (thid >= 0) {
        sceKernelStartThread(thid, 0, NULL);
    }

    int trp_handle = 0;
    uint32_t count = 0;
    int res_h = sceNpTrophyCreateHandle(&trp_handle);
    if (res_h >= 0) {
        sceNpTrophyGetTrophyUnlockState(trp_ctx, trp_handle, &trophies_unlocks, &count);
        sceNpTrophyDestroyHandle(trp_handle);
    }

    trophies_available = 1;
    l_success("PS Vita Trophy subsystem successfully initialized.");
    return 0;
}

uint8_t trophies_is_unlocked(uint32_t id) {
    if (!trophies_available) {
        return 0;
    }
    return (trophies_unlocks.unk[id >> 5] & (1 << (id & 31))) != 0;
}

void trophies_unlock(uint32_t id) {
    if (!trophies_available) {
        l_warn("trophies_unlock(%u) called but trophies not available", (unsigned)id);
        return;
    }

    if (trophies_is_unlocked(id)) {
        return; // Already unlocked
    }

    // Mark as unlocked in cache
    trophies_unlocks.unk[id >> 5] |= (1 << (id & 31));

    // Enqueue
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
