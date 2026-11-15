# Third-party components and licenses

This repository bundles selected upstream components to make the ESP-IDF project easier to build.

| Component | Version | License | Source |
| --- | --- | --- | --- |
| LVGL | 9.1.0 | MIT | https://github.com/lvgl/lvgl/tree/v9.1.0 |
| Espressif `esp_lcd_touch` | 1.1.2 | Apache-2.0 | https://github.com/espressif/esp-bsp |
| Espressif `esp_lcd_touch_gt911` | 1.1.3 | Apache-2.0 | https://github.com/espressif/esp-bsp |
| ESP-IDF (external toolchain) | 5.5.x | Multiple upstream licenses | https://github.com/espressif/esp-idf |

License copies are retained in each component folder. `components/lvgl__lvgl/LICENCE.txt` restores the MIT notice associated with the upstream LVGL 9.1.0 distribution.

Some project hardware-port code carries its own Espressif SPDX notices (including CC0-1.0); those upstream notices take precedence over the top-level MIT license for that code.
