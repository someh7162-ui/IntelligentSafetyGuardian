#ifndef RIDERGUARD_ALERT_H
#define RIDERGUARD_ALERT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    RG_LED_GREEN = 0,
    RG_LED_AMBER = 1,
    RG_LED_RED = 2
} rg_led_t;

typedef struct {
    float limit_kph;
    uint64_t valid_until_ms;
    bool has_policy;
} rg_alert_policy_t;

typedef struct {
    bool active_approach;
    bool red_warning;
    uint64_t valid_until_ms;
} rg_signal_alert_t;

typedef void (*rg_set_buzzer_fn)(bool enabled);
typedef void (*rg_set_led_fn)(rg_led_t color);

/* Called when a telemetry response provides mode, limitKph and validUntilMs. */
void rg_alert_apply_policy(rg_alert_policy_t *state, float limit_kph, uint64_t valid_until_ms);

/* Called every speed sample, independently of network availability. */
bool rg_alert_update(const rg_alert_policy_t *state, float speed_kph, uint64_t now_ms,
                     rg_set_buzzer_fn buzzer, rg_set_led_fn led);

/* Optional simulated signal reminder. No signal means the existing speed policy applies. */
bool rg_alert_update_with_signal(const rg_alert_policy_t *speed, const rg_signal_alert_t *signal,
                                 float speed_kph, uint64_t now_ms,
                                 rg_set_buzzer_fn buzzer, rg_set_led_fn led);

#endif
