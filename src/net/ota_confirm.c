#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#if defined(CONFIG_BOOTLOADER_MCUBOOT)
#include <zephyr/dfu/mcuboot.h>
#endif

#include "ota_confirm.h"
#include "net.h"

LOG_MODULE_REGISTER(ota_confirm, LOG_LEVEL_INF);

#if defined(CONFIG_BOOTLOADER_MCUBOOT)

#define OTA_CONFIRM_RETRY_DELAY K_SECONDS(1)
#define OTA_CONFIRM_START_DELAY K_SECONDS(5)

static struct k_work_delayable ota_confirm_work;

static const char *swap_type_to_str(int swap_type)
{
    switch (swap_type) {
    case BOOT_SWAP_TYPE_NONE:
        return "none";
    case BOOT_SWAP_TYPE_TEST:
        return "test";
    case BOOT_SWAP_TYPE_PERM:
        return "perm";
    case BOOT_SWAP_TYPE_REVERT:
        return "revert";
    case BOOT_SWAP_TYPE_FAIL:
        return "fail";
    default:
        return "unknown";
    }
}

static void ota_confirm_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (boot_is_img_confirmed()) {
        return;
    }

    if (!network_ready) {
        LOG_WRN("Image still unconfirmed; waiting for network readiness");
        k_work_reschedule(&ota_confirm_work, OTA_CONFIRM_RETRY_DELAY);
        return;
    }

    int ret = boot_write_img_confirmed();
    if (ret == 0) {
        LOG_INF("Confirmed running image after successful startup");
        return;
    }

    LOG_ERR("Failed to confirm running image: %d", ret);
    k_work_reschedule(&ota_confirm_work, OTA_CONFIRM_RETRY_DELAY);
}

void ota_confirm_init(void)
{
    int swap_type = mcuboot_swap_type();
    bool confirmed = boot_is_img_confirmed();

    LOG_INF("MCUboot image state: swap=%s confirmed=%d",
            swap_type_to_str(swap_type), confirmed);

    if (confirmed) {
        return;
    }

    k_work_init_delayable(&ota_confirm_work, ota_confirm_work_handler);
    k_work_schedule(&ota_confirm_work, OTA_CONFIRM_START_DELAY);
    LOG_WRN("Running unconfirmed image; will confirm after network comes up");
}

#else

void ota_confirm_init(void)
{
}

#endif
