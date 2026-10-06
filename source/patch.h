#ifndef __PATCH_H__
#define __PATCH_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize all function hooks and memory patches on the loaded .so modules.
 */
void so_patch(void);

/**
 * @brief Set whether on-screen virtual touch controls should be visible or hidden.
 * @param visible 1 for visible, 0 for hidden.
 */
void controls_set_visible(int visible);

/**
 * @brief Get whether on-screen virtual touch controls are currently set to visible.
 * @return 1 if visible, 0 if hidden.
 */
int controls_is_visible(void);

/**
 * @brief Get the active ControlsLayer singleton pointer if available.
 * @return Pointer to ControlsLayer, or NULL.
 */
void *controls_get_layer(void);

/**
 * @brief Update the opacity of all 12 virtual touch controls on the given ControlsLayer.
 *        When hidden, a fixed 1% opacity (~2/255) is applied so that buttons remain
 *        invisible on screen even after death/checkpoint reload, while preserving
 *        engine state.
 * @param controlsLayer Pointer to ControlsLayer.
 * @param visible 1 for 100% opacity, 0 for 1% opacity.
 */
void controls_update_opacity(void *controlsLayer, int visible);

#ifdef __cplusplus
}
#endif

#endif // __PATCH_H__
