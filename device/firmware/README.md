# STM32 alert adapter

`riderguard_alert.c` contains the device-side speed comparison and buzzer/LED decision. Add it to the STM32Cube project and provide `rg_set_buzzer_fn` and `rg_set_led_fn` callbacks for the actual GPIO pins. Call `rg_alert_apply_policy` after each telemetry response, then call `rg_alert_update` on every speed sample. The device keeps its last received limit during a network outage and shows amber when the policy expires.

The OV2640 DCMI pins, 4G AT command sequence, power circuit, and actual buzzer/LED GPIO cannot be assigned before the exact board and 4G SKU are verified. Do not connect the LiPo or vehicle wiring until the protection and power tests in `docs/外卖安全维护系统_硬件方案可行性评审.md` are completed.
