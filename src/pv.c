#include "pv.h"
#include "logging.h"
#include <math.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#define MPPT_PERTURB_INTERVAL_SEC 0.1  /* 100 ms */
#define FAULT_VOLTAGE_THRESHOLD 0.3   /* 30% voltage deviation */
#define FAULT_CURRENT_THRESHOLD 0.5   /* 50% current imbalance */
#define MIN_VALID_VOLTAGE 0.1         /* minimal sensible PV voltage (V) */
#define MIN_VALID_CURRENT 0.0         /* minimal sensible PV current (A) */

static const char* pv_state_str[] = {
    "OFF", "STARTING", "MPPT", "CURTAILED", "FAULT", "MAINTENANCE"
};

static const char* mppt_algorithm_str[] = {
    "OFF", "PERTURB_OBSERVE", "INCREMENTAL_CONDUCTANCE", "CONSTANT_VOLTAGE"
};

/* Helper: monotonic seconds as double */
static double monotonic_seconds(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return ts.tv_sec + ts.tv_nsec / 1e9;
    }
    return (double)time(NULL);
}

/* Initialize PV system context with safe defaults */
int pv_init(pv_system_t* pv, const system_config_t* config) {
    if (!pv || !config) return -1;

    /* zero everything first, then set explicit defaults */
    memset(pv, 0, sizeof(pv_system_t));

    pv->state = PV_STATE_MPPT;
    pv->mppt_algorithm = MPPT_PERTURB_OBSERVE;
    pv->mppt_step_size = 0.5;          /* volts per step */
    pv->mppt_voltage_ref = 0.0;
    pv->mppt_power_ref = 0.0;

    pv->active_string_count = 0;
    pv->total_capacity = 0.0;
    pv->available_power = 0.0;
    pv->max_operating_power = 0.0;

    pv->daily_energy = 0.0;
    pv->monthly_energy = 0.0;
    pv->total_energy = 0.0;

    pv->fault_count = 0;
    pv->last_fault_time = 0;
    pv->last_reset_time = time(NULL);
    pv->last_fault_reason[0] = '\0';

    /* Initialize strings with sane defaults and accumulate capacity */
    for (int i = 0; i < MAX_PV_STRINGS; ++i) {
        /* Defensive: ensure string_id buffer exists in struct */
        snprintf(pv->strings[i].string_id, sizeof(pv->strings[i].string_id),
                 "PV_STRING_%d", i + 1);

        pv->strings[i].max_power   = 5000.0; /* W */
        pv->strings[i].max_voltage = 600.0;  /* V (Voc-like) */
        pv->strings[i].max_current = 10.0;   /* A */
        pv->strings[i].enabled     = true;
        pv->strings[i].fault       = false;
        pv->strings[i].efficiency  = 98.5;   /* percent-ish */
        /* Optional sensible per-string defaults if those fields exist */
        /* pv->strings[i].irradiance = 1000.0; pv->strings[i].temperature = 25.0; */

        if (pv->strings[i].enabled && !pv->strings[i].fault) {
            pv->active_string_count++;
            pv->total_capacity += pv->strings[i].max_power;
        }
    }

    pv->max_operating_power = pv->total_capacity;

    return 0;
}

// Update PV measurements and accumulate energy
void pv_update_measurements(pv_system_t* pv, system_measurements_t* measurements) {
    if (!pv || !measurements) return;

    int active_strings = 0;
    double available_power = pv_calculate_available_power(pv, measurements);

    for (int i = 0; i < MAX_PV_STRINGS; ++i) {
        measurements->pv_voltage[i] = 0.9;
        measurements->pv_current[i] = 0.8;
        active_strings++;
    }

    measurements->pv_power_total = available_power;
    measurements->pv_strings_active = active_strings;
    pv->active_string_count = active_strings;
    pv->available_power = available_power;

    // Energy integration (Wh) using monotonic time deltas
    static double last_t = 0.0;
    double now = monotonic_seconds();

    if (last_t > 0.0 && now > last_t) {
        double dt = now - last_t;          // seconds
        double Wh = available_power * (dt / 3600.0);
        pv->daily_energy += Wh;
        pv->total_energy += Wh;
    }
    
    last_t = now;
}

/* Estimate available power using derating factors */
double pv_calculate_available_power(pv_system_t* pv, system_measurements_t* measurements) {
    if (!pv || !measurements) return 0.0;

    double available_power = 0.0;

    for (int i = 0; i < MAX_PV_STRINGS; ++i) {
        if (!pv->strings[i].enabled || pv->strings[i].fault) continue;

        /* default derating factors; these can be replaced by real sensors later */
        double irradiance_factor = 1.0;
        double temp_factor = 1.0;
        double soiling_factor = 0.98;
        double wiring_loss = 0.97;

        double max_string = pv->strings[i].max_power
                            * irradiance_factor
                            * temp_factor
                            * soiling_factor
                            * wiring_loss;

        available_power += max_string;
    }

    /* ensure we never claim more than installed capacity */
    if (available_power > pv->total_capacity) available_power = pv->total_capacity;

    return available_power;
}

/* MPPT routine with monotonic timing and safety guards */
void pv_run_mppt(pv_system_t* pv, system_measurements_t* measurements) {
    if (!pv || !measurements) return;
    if (pv->state != PV_STATE_MPPT) return;

    /* Skip MPPT if there is effectively no PV production (night / sensor failure) */
    if (measurements->pv_power_total <= 0.1) {
        LOG_DEBUG("pv_run_mppt: skipping, pv_power_total=%.3f", measurements->pv_power_total);
        return;
    }

    static double last_mppt_time = 0.0;
    double now = monotonic_seconds();
    if (last_mppt_time > 0.0 && (now - last_mppt_time) < MPPT_PERTURB_INTERVAL_SEC) {
        return;
    }
    last_mppt_time = now;

    /* Ensure there is at least one valid voltage to reference */
    double v_ref_measure = 0.0;
    bool have_voltage = false;
    for (int i = 0; i < MAX_PV_STRINGS; ++i) {
        double v = measurements->pv_voltage[i];
        if (isfinite(v) && v >= MIN_VALID_VOLTAGE) {
            v_ref_measure = v;
            have_voltage = true;
            break;
        }
    }

    switch (pv->mppt_algorithm) {
        case MPPT_PERTURB_OBSERVE: {
            double current_power = measurements->pv_power_total;
            double current_voltage = have_voltage ? v_ref_measure : pv->mppt_voltage_ref;

            if (pv->mppt_power_ref <= 0.0) {
                /* first-run initialization */
                pv->mppt_power_ref = current_power;
                pv->mppt_voltage_ref = current_voltage > 0.0 ? current_voltage : (pv->strings[0].max_voltage * 0.78);
            } else {
                if (current_power > pv->mppt_power_ref) {
                    /* continue same direction */
                    pv->mppt_voltage_ref += (current_voltage > pv->mppt_voltage_ref) ?
                                            pv->mppt_step_size : -pv->mppt_step_size;
                } else {
                    /* reverse direction */
                    pv->mppt_voltage_ref += (current_voltage > pv->mppt_voltage_ref) ?
                                            -pv->mppt_step_size : pv->mppt_step_size;
                }
                pv->mppt_power_ref = current_power;
            }
            break;
        }

        case MPPT_INCREMENTAL_CONDUCTANCE: {
            static double prev_power = 0.0;
            static double prev_voltage = 0.0;

            double prevP = prev_power;
            double prevV = prev_voltage;
            double curP = measurements->pv_power_total;
            double curV = have_voltage ? v_ref_measure : prevV;

            double deltaV = curV - prevV;
            double deltaP = curP - prevP;

            if (fabs(deltaV) > 1e-6 && fabs(curV) > 1e-6) {
                double g = (curP / curV);
                double dg = (deltaP / deltaV);

                if (fabs(dg) > fabs(g)) {
                    pv->mppt_voltage_ref += pv->mppt_step_size;
                } else {
                    pv->mppt_voltage_ref -= pv->mppt_step_size;
                }
            }
            prev_power = curP;
            prev_voltage = curV;
            break;
        }

        case MPPT_CONSTANT_VOLTAGE:
            /* typical constant fraction of Voc; use string max_voltage as proxy for Voc */
            pv->mppt_voltage_ref = pv->strings[0].max_voltage * 0.78;
            break;

        case MPPT_OFF:
        default:
            pv->mppt_voltage_ref = 0.0;
            break;
    }

    /* Safe clamping: derive safe bounds from string ratings (use string 0 as proxy) */
    double min_voltage = pv->strings[0].max_voltage * 0.5;
    double max_voltage = pv->strings[0].max_voltage * 0.95;

    if (pv->mppt_voltage_ref < min_voltage) pv->mppt_voltage_ref = min_voltage;
    if (pv->mppt_voltage_ref > max_voltage) pv->mppt_voltage_ref = max_voltage;

    LOG_DEBUG("pv_run_mppt: alg=%s mppt_vref=%.3f mppt_pref=%.3f",
              mppt_algorithm_str[pv->mppt_algorithm],
              pv->mppt_voltage_ref,
              pv->mppt_power_ref);
}

/* Apply curtailment percentage (0..100) */
void pv_apply_curtailment(pv_system_t* pv, double curtail_percent) {
    if (!pv) return;

    if (isnan(curtail_percent)) return;

    if (curtail_percent < 0.0) curtail_percent = 0.0;
    if (curtail_percent > 100.0) curtail_percent = 100.0;

    pv->max_operating_power = pv->total_capacity * (1.0 - curtail_percent / 100.0);

    if (curtail_percent > 0.0) {
        pv->state = PV_STATE_CURTAILED;
    } else if (pv->state == PV_STATE_CURTAILED) {
        pv->state = PV_STATE_MPPT;
    }

    LOG_DEBUG("pv_apply_curtailment: percent=%.2f max_op=%.1f state=%s",
              curtail_percent, pv->max_operating_power, pv_state_str[pv->state]);
}

/* Fault detection with a small debounce (two consecutive readings) */
bool pv_detect_faults(pv_system_t* pv, system_measurements_t* measurements) {
    if (!pv || !measurements) return false;

    bool fault_detected = false;
    static int consecutive_faults[MAX_PV_STRINGS] = {0};
    const int debounce_count = 2;

    for (int i = 0; i < MAX_PV_STRINGS; ++i) {
        if (!pv->strings[i].enabled) {
            consecutive_faults[i] = 0;
            continue;
        }

        bool this_cycle_fault = false;
        double V = measurements->pv_voltage[i];
        double I = measurements->pv_current[i];

        /* Overvoltage: use a margin over max_voltage */
        if (isfinite(V) && V > (pv->strings[i].max_voltage * 1.1)) {
            this_cycle_fault = true;
            LOG_DEBUG("pv_detect_faults: string %d overvoltage V=%.3f limit=%.3f", i, V, pv->strings[i].max_voltage * 1.1);
        }

        /* Overcurrent */
        if (isfinite(I) && I > (pv->strings[i].max_current * 1.2)) {
            this_cycle_fault = true;
            LOG_DEBUG("pv_detect_faults: string %d overcurrent I=%.3f limit=%.3f", i, I, pv->strings[i].max_current * 1.2);
        }

        /* Voltage imbalance relative to string 0 when index > 0 */
        if (i > 0) {
            double V0 = measurements->pv_voltage[0];
            if (isfinite(V) && isfinite(V0) && V0 > MIN_VALID_VOLTAGE) {
                double voltage_diff = fabs(V - V0);
                if (voltage_diff > (V0 * FAULT_VOLTAGE_THRESHOLD)) {
                    this_cycle_fault = true;
                    LOG_DEBUG("pv_detect_faults: string %d voltage imbalance diff=%.3f base=%.3f", i, voltage_diff, V0);
                }
            }
        }

        if (this_cycle_fault) {
            consecutive_faults[i] = (consecutive_faults[i] < debounce_count) ? (consecutive_faults[i] + 1) : debounce_count;
        } else {
            consecutive_faults[i] = 0;
        }

        if (consecutive_faults[i] >= debounce_count) {
            if (!pv->strings[i].fault) {
                pv->strings[i].fault = true;
                strncpy(pv->last_fault_reason, "String fault", sizeof(pv->last_fault_reason) - 1);
                pv->last_fault_reason[sizeof(pv->last_fault_reason)-1] = '\0';
                pv->last_fault_time = time(NULL);
                LOG_DEBUG("pv_detect_faults: string %d marked fault", i);
            }
            fault_detected = true;
        }
    }

    if (fault_detected) {
        pv->state = PV_STATE_FAULT;
        pv->fault_count++;
        LOG_DEBUG("pv_detect_faults: fault_detected total_fault_count=%u", pv->fault_count);
    }

    return fault_detected;
}

/* Clear faults and reset related state */
void pv_clear_faults(pv_system_t* pv) {
    if (!pv) return;

    for (int i = 0; i < MAX_PV_STRINGS; ++i) {
        pv->strings[i].fault = false;
    }

    if (pv->state == PV_STATE_FAULT) {
        pv->state = PV_STATE_MPPT;
    }

    pv->last_fault_reason[0] = '\0';
    pv->last_fault_time = 0;

    LOG_DEBUG("pv_clear_faults: faults cleared, state=%s", pv_state_str[pv->state]);
}

/* Human-readable status logging */
void pv_log_status(const pv_system_t* pv) {
    if (!pv) return;

    printf("=== PV System Status ===\n");
    printf("State: %s\n", pv_state_str[pv->state]);
    printf("MPPT Algorithm: %s\n", mppt_algorithm_str[pv->mppt_algorithm]);
    printf("Active Strings: %d/%d\n", pv->active_string_count, MAX_PV_STRINGS);
    printf("Total Capacity: %.1f W\n", pv->total_capacity);
    printf("Available Power: %.1f W\n", pv->available_power);
    printf("Max Operating Power: %.1f W\n", pv->max_operating_power);
    printf("Daily Energy: %.2f kWh\n", pv->daily_energy / 1000.0);
    printf("Total Energy: %.2f kWh\n", pv->total_energy / 1000.0);
    printf("Fault Count: %u\n", pv->fault_count);

    if (pv->state == PV_STATE_FAULT) {
        printf("Last Fault: %s at %s", pv->last_fault_reason,
               pv->last_fault_time ? ctime(&pv->last_fault_time) : "unknown\n");
    }
    printf("========================\n");
}

/* Average efficiency across active, healthy strings */
double pv_get_efficiency(const pv_system_t* pv) {
    if (!pv) return 0.0;

    double total_eff = 0.0;
    int count = 0;

    for (int i = 0; i < MAX_PV_STRINGS; ++i) {
        if (pv->strings[i].enabled && !pv->strings[i].fault) {
            total_eff += pv->strings[i].efficiency;
            count++;
        }
    }

    return count > 0 ? (total_eff / count) : 0.0;
}
