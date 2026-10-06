#ifndef __TROPHIES_H__
#define __TROPHIES_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the PS Vita trophy subsystem (sceNpTrophy + NoTrpDrm).
 * @return 0 on success, negative error code on failure.
 */
int trophies_init(void);

/**
 * @brief Trigger an unlock for trophy ID.
 * @param id Trophy ID (0 = Platinum, 1..17 = Game achievements).
 */
void trophies_unlock(uint32_t id);

/**
 * @brief Check if trophy ID is currently unlocked.
 * @param id Trophy ID.
 * @return 1 if unlocked, 0 otherwise.
 */
uint8_t trophies_is_unlocked(uint32_t id);

#ifdef __cplusplus
}
#endif

#endif // __TROPHIES_H__
