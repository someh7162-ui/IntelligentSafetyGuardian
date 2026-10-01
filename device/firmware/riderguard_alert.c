#include "riderguard_alert.h"

void rg_alert_apply_policy(rg_alert_policy_t *state, float limit_kph, uint64_t valid_until_ms) {
    if (state == 0 || limit_kph <= 0.0f || limit_kph > 80.0f) return;
    state->limit_kph = limit_kph;
    state->valid_until_ms = valid_until_ms;
    state->has_policy = true;
}

bool rg_alert_update(const rg_alert_policy_t *state, float speed_kph, uint64_t now_ms,
                     rg_set_buzzer_fn buzzer, rg_set_led_fn led) {
    if (state == 0 || buzzer == 0 || led == 0) return false;
    /* Retain the last limit when offline. Amber warns that the cloud policy is stale. */
    bool over = state->has_policy && speed_kph > state->limit_kph;
    buzzer(over);
    led(over ? RG_LED_RED : (!state->has_policy || now_ms > state->valid_until_ms) ? RG_LED_AMBER : RG_LED_GREEN);
    return over;
}

bool rg_alert_update_with_signal(const rg_alert_policy_t *speed, const rg_signal_alert_t *signal,
                                 float speed_kph, uint64_t now_ms,
                                 rg_set_buzzer_fn buzzer, rg_set_led_fn led) {
    if (speed == 0 || buzzer == 0 || led == 0) return false;
    bool over = speed->has_policy && speed_kph > speed->limit_kph;
    bool signal_stale = signal != 0 && signal->active_approach && now_ms > signal->valid_until_ms;
    bool signal_red = signal != 0 && signal->active_approach && !signal_stale && signal->red_warning;
    bool warning = over || signal_red;
    buzzer(warning);
    led(warning ? RG_LED_RED : (!speed->has_policy || now_ms > speed->valid_until_ms || signal_stale)
        ? RG_LED_AMBER : RG_LED_GREEN);
    return warning;
}
