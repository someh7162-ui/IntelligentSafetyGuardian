# STM32 alert adapter

`riderguard_alert.c` contains the device-side speed comparison and buzzer/LED decision. Add it to the STM32Cube project and provide `rg_set_buzzer_fn` and `rg_set_led_fn` callbacks for the actual GPIO pins. Call `rg_alert_apply_policy` after each telemetry response, then call `rg_alert_update` on every speed sample. The device keeps its last received limit during a network outage and shows amber when the policy expires.

The optional `rg_alert_update_with_signal` combines the existing speed warning with `alerts.trafficSignal`. Set `active_approach` only for a matched approach and copy `trafficSignal.validUntilMs`; an expired active signal turns the LED amber. The current signal source is explicitly simulated and must not be treated as a real-road traffic instruction. GPIO wiring and short buzzer pulse timing remain board-specific integration work.

The OV2640 DCMI pins, 4G AT command sequence, power circuit, and actual buzzer/LED GPIO cannot be assigned before the exact board and 4G SKU are verified. Do not connect the LiPo or vehicle wiring until the protection and power tests in `docs/外卖安全维护系统_硬件方案可行性评审.md` are completed.

## STM32F407 摄像头/GPS 实机工程

[stm32f407/](stm32f407/) 是现有 STM32CubeIDE 工程，已实现 OV2640 JPEG、MPU6050 初始化和 USART2 GNSS RMC USB 输出；默认运行摄像头模式。接线及电脑网关联动见 [部署联动文档](../../docs/RiderGuard_STM32_GPS_部署联动.md)。本目录的 iderguard_alert.c/.h 是独立策略模块，尚未并入 STM32 工程的实际 GPIO 回调。
