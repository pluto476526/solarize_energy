/* battery.c — production-grade battery management (LFP defaults) */

#include "battery.h"
#include "logging.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

/* --- Voltage->SOC lookup for LFP (pack per-cell OCV mapping) ---
   This table maps single-cell open-circuit voltage (V) to SOC (%).
   Values approximate a common LFP curve. We'll interpolate.
*/
typedef struct { double v; double soc; } ocv_point_t;
static const ocv_point_t lfp_ocv_table[] = {
    {2.80, 0.0}, {3.00, 2.0}, {3.10, 10.0}, {3.20, 30.0},
    {3.25, 50.0}, {3.30, 70.0}, {3.35, 85.0}, {3.40, 95.0}, {3.45, 100.0}
};
static const int lfp_ocv_len = sizeof(lfp_ocv_table) / sizeof(lfp_ocv_table[0]);

/* Helper: linear interpolation on ocv table */
static double ocv_to_soc_lfp(double cell_v) {
    if (cell_v <= lfp_ocv_table[0].v) return lfp_ocv_table[0].soc;
    if (cell_v >= lfp_ocv_table[lfp_ocv_len-1].v) return lfp_ocv_table[lfp_ocv_len-1].soc;
    for (int i = 1; i < lfp_ocv_len; ++i) {
        if (cell_v <= lfp_ocv_table[i].v) {
            double x0 = lfp_ocv_table[i-1].v, y0 = lfp_ocv_table[i-1].soc;
            double x1 = lfp_ocv_table[i].v, y1 = lfp_ocv_table[i].soc;
            double t = (cell_v - x0) / (x1 - x0);
            return y0 + t * (y1 - y0);
        }
    }
    return 0.0;
}

/* --- Defaults chosen: modern LFP banks, multi-bank support --- */
#define DEFAULT_BANK_NOMINAL_V 48.0
#define DEFAULT_BANK_SERIES_CELLS 16   /* ~48 V nominal */
#define DEFAULT_BANK_PARALLEL_STRINGS 1
#define DEFAULT_BANK_CAPACITY_WH 10000.0  /* 10 kWh per bank */
#define DEFAULT_BANK_MAX_CHARGE_W 5000.0
#define DEFAULT_BANK_MAX_DISCHARGE_W 5000.0

/* Initialize battery system with sane production defaults */
int battery_init(battery_system_t* bat, const system_config_t* config) {
    if (!bat) return -1;
    memset(bat, 0, sizeof(*bat));

    /* Default to LFP chemistry and multi-bank defaults */
    bat->chemistry = BAT_CHEM_LFP;
    bat->bank_count = MAX_BATTERY_BANKS;
    bat->active_bank_count = MAX_BATTERY_BANKS;

    bat->capacity_nominal_wh = 0.0;
    for (int i = 0; i < bat->bank_count; ++i) {
        battery_bank_t *b = &bat->banks[i];
        snprintf(b->bank_id, sizeof(b->bank_id), "BANK_%d", i+1);
        b->nominal_voltage = DEFAULT_BANK_NOMINAL_V;
        b->cells_in_series = DEFAULT_BANK_SERIES_CELLS;
        b->parallel_strings = DEFAULT_BANK_PARALLEL_STRINGS;
        b->capacity_wh = DEFAULT_BANK_CAPACITY_WH;
        b->max_charge_power = DEFAULT_BANK_MAX_CHARGE_W;
        b->max_discharge_power = DEFAULT_BANK_MAX_DISCHARGE_W;
        b->cycle_count = 0;
        b->last_full_charge_ts = 0;
        bat->capacity_nominal_wh += b->capacity_wh;
    }

    /* Initialize state */
    bat->accumulated_ah = 0.0;
    bat->last_update_ts = 0;
    bat->soc_coulomb = 50.0;
    bat->soc_voltage = 50.0;
    bat->soc_estimated = 50.0;
    bat->soc_smoothed = 50.0;
    bat->capacity_remaining_wh = bat->capacity_nominal_wh * 0.5;
    bat->health_percent = 100.0;

    /* Limits — conservative factory defaults; can be tuned at runtime */
    /* Convert power limits to currents using typical nominal voltage */
    double pack_v = bat->banks[0].nominal_voltage;
    bat->max_charge_power_w = bat->capacity_nominal_wh > 0 ? (bat->banks[0].max_charge_power * bat->bank_count) : 0.0;
    bat->max_discharge_power_w = bat->capacity_nominal_wh > 0 ? (bat->banks[0].max_discharge_power * bat->bank_count) : 0.0;

    bat->max_charge_current_a = bat->max_charge_power_w / pack_v;
    bat->max_discharge_current_a = bat->max_discharge_power_w / pack_v;

    /* Timing defaults */
    bat->absorption_duration_s = 2.0 * 3600.0;  /* 2 hours */
    bat->float_duration_s = 24.0 * 3600.0;      /* 24 hours */

    /* Misc */
    bat->temperature_c = 25.0;
    bat->ambient_temperature_c = 25.0;
    bat->cooling_active = false;
    bat->heating_active = false;
    bat->state = BATTERY_STATE_IDLE;
    bat->charge_stage = CHARGE_BULK;

    /* SOC fusion parameters */
    bat->soc_voltage_weight = 0.4;   /* weight for voltage-based (0..1) */
    bat->soc_smoothing_alpha = 0.10; /* smoothing factor */
    bat->min_operating_soc = 5.0;
    bat->max_operating_soc = 98.0;

    /* Clear faults */
    bat->overvoltage_fault = bat->undervoltage_fault = false;
    bat->overcurrent_fault = bat->overtemperature_fault = false;
    bat->last_fault_reason[0] = '\0';

    (void)config;

    LOG_INFO("Battery initialized: %d banks, nominal_wh=%.0f", bat->active_bank_count, bat->capacity_nominal_wh);
    return 0;
}

/* Update measurements: compute power, SOC, safety, and thermal actions */
void battery_update_measurements(battery_system_t* bat, system_measurements_t* measurements) {
    if (!bat || !measurements) return;

    /* Update temperature from measurements if provided */
    if (measurements->battery_temp != 0.0) {
        bat->temperature_c = measurements->battery_temp;
    }

    /* Compute battery power (W) using measured voltage and current */
    measurements->battery_power = measurements->battery_voltage * measurements->battery_current;
    measurements->timestamp = measurements->timestamp ? measurements->timestamp : time(NULL);

    /* Recalculate SOC using measured values */
    battery_calculate_soc(bat, measurements);

    /* Update safety and thermal controls */
    battery_check_limits(bat, measurements);
    battery_thermal_management(bat);

    /* Keep capacity_remaining consistent */
    bat->capacity_remaining_wh = bat->capacity_nominal_wh * (bat->soc_smoothed / 100.0);
}

/* Compute SOC (coulomb + voltage fusion), store in bat and measurements */
double battery_calculate_soc(battery_system_t* bat, system_measurements_t* measurements) {
    if (!bat || !measurements) return 0.0;

    time_t now = measurements->timestamp ? measurements->timestamp : time(NULL);

    /* --- Coulomb counting (store state inside object) --- */
    if (bat->last_update_ts == 0) {
        /* first time: initialize from a reasonable default or provided SOC */
        bat->last_update_ts = now;
    } else {
        double dt_s = difftime(now, bat->last_update_ts);
        /* pack current in A, dt in seconds => Ah = A * s / 3600 */
        double delta_ah = measurements->battery_current * dt_s / 3600.0;
        bat->accumulated_ah += delta_ah;
        bat->last_update_ts = now;
    }

    /* Convert accumulated Ah to percentage: total Ah = Wh / Vpack */
    double pack_v = (bat->banks[0].nominal_voltage > 0.0) ? bat->banks[0].nominal_voltage : 48.0;
    double total_ah = (bat->capacity_nominal_wh / pack_v);
    if (total_ah <= 0.0) total_ah = 1.0; /* guard */

    /* Coulomb SOC (0..100) —  assume accumulated_ah is net Ah since last full charge
       We store accumulated_ah as positive for charge, negative for discharge (consistent with current sign)
       We compute coulomb SOC relative to a nominal "0%" baseline at accumulated_ah = -total_ah
       To simplify: maintain a coulomb SOC anchored by capacity_remaining_wh
    */
    double coulomb_ah = bat->capacity_remaining_wh / pack_v; /* current remaining Ah */
    bat->soc_coulomb = 100.0 * (coulomb_ah / total_ah);

    /* --- Voltage-based SOC --- */
    double cell_v = 0.0;
    if (bat->banks[0].cells_in_series > 0) {
        cell_v = measurements->battery_voltage / (double)bat->banks[0].cells_in_series;
    } else {
        cell_v = measurements->battery_voltage / 16.0; /* fallback */
    }

    /* Use chemistry-specific mapping; default LFP */
    if (bat->chemistry == BAT_CHEM_LFP) {
        bat->soc_voltage = ocv_to_soc_lfp(cell_v);
    } else {
        /* Fallback linear mapping if unknown chemistry */
        double vmin = 2.8, vmax = 4.2;
        double t = (cell_v - vmin) / (vmax - vmin);
        bat->soc_voltage = fmin(fmax(t * 100.0, 0.0), 100.0);
    }

    /* --- Fusion: weight coulomb vs voltage ---
       Use dynamic weight: favor coulomb at higher currents, voltage at rest.
       For simplicity, use configured weight and clamp.
    */
    double wv = bat->soc_voltage_weight;
    if (wv < 0.0) wv = 0.0;
    if (wv > 1.0) wv = 1.0;
    double coulomb_weight = 1.0 - wv;

    bat->soc_estimated = coulomb_weight * bat->soc_coulomb + wv * bat->soc_voltage;

    /* --- Smoothing (exponential) to avoid jitter in control loops --- */
    double alpha = bat->soc_smoothing_alpha;
    if (alpha <= 0.0) alpha = 0.1;
    if (alpha > 1.0) alpha = 1.0;
    bat->soc_smoothed = alpha * bat->soc_estimated + (1.0 - alpha) * bat->soc_smoothed;

    /* Clamp ranges */
    if (bat->soc_smoothed < 0.0) bat->soc_smoothed = 0.0;
    if (bat->soc_smoothed > 100.0) bat->soc_smoothed = 100.0;

    /* Update measurements and capacity remaining */
    measurements->battery_soc = bat->soc_smoothed;
    bat->capacity_remaining_wh = bat->capacity_nominal_wh * (bat->soc_smoothed / 100.0);

    return bat->soc_smoothed;
}

/* Manage charging using available PV/excess power and limits */
void battery_manage_charging(battery_system_t* bat, double available_power, double load_power) {
    if (!bat) return;

    double excess = available_power - load_power;
    double max_charge = battery_calculate_max_charge(bat);
    bool do_charge = (excess > 100.0) && (bat->soc_smoothed < bat->max_operating_soc);

    if (!do_charge) {
        /* Idle / reset absorption timers */
        bat->state = BATTERY_STATE_IDLE;
        bat->absorption_start_ts = 0;
        bat->charge_stage = CHARGE_BULK;
        return;
    }

    bat->state = BATTERY_STATE_CHARGING;

    /* Proposed charge power is min(excess, max_charge) */
    double charge_power = fmin(excess, max_charge);

    /* Stage logic */
    if (bat->soc_smoothed < 85.0) {
        bat->charge_stage = CHARGE_BULK;
        /* constant-power charging within limits */
    } else if (bat->soc_smoothed < 95.0) {
        if (bat->absorption_start_ts == 0) bat->absorption_start_ts = time(NULL);
        bat->charge_stage = CHARGE_ABSORPTION;

        /* Ramp down power during absorption */
        double elapsed = difftime(time(NULL), bat->absorption_start_ts);
        double factor = 1.0;
        if (bat->absorption_duration_s > 0.0) {
            factor = fmax(0.1, 1.0 - (elapsed / bat->absorption_duration_s));
        }
        charge_power *= factor;
    } else {
        bat->charge_stage = CHARGE_FLOAT;
        charge_power = fmin(charge_power, max_charge * 0.05); /* float ~5% */
        if (bat->float_start_ts == 0) bat->float_start_ts = time(NULL);
    }

    /* Temperature derate */
    if (bat->temperature_c > 40.0) charge_power *= 0.5;
    if (bat->temperature_c < 0.0) charge_power = 0.0; /* no charge below freezing */

    /* Update energy statistics (Wh) */
    /* charge_power is instant W — we'll account per-second in control loop; add a nominal increment */
    bat->total_charge_wh += charge_power * (bat->absorption_duration_s > 0 ? 1.0 / 3600.0 : 0.0); /* safe placeholder */

    /* Set state fields */
    (void)charge_power; /* actual actuator code should use this to set setpoints */
}

/* Manage discharging when islanded or requested */
void battery_manage_discharging(battery_system_t* bat, double load_power, bool grid_available) {
    if (!bat) return;

    double max_dis = battery_calculate_max_discharge(bat);
    bool should_discharge = false;

    if (!grid_available) {
        should_discharge = (load_power > 100.0) && (bat->soc_smoothed > bat->min_operating_soc);
    } else {
        /* Default: do not discharge when grid available; policy may override */
        should_discharge = false;
    }

    if (!should_discharge) {
        bat->state = BATTERY_STATE_IDLE;
        return;
    }

    bat->state = BATTERY_STATE_DISCHARGING;

    double discharge_power = fmin(load_power, max_dis);

    /* Protect minimum SOC */
    double projected_wh = bat->capacity_remaining_wh - discharge_power * 1.0; /* per-second placeholder */
    if ((projected_wh / bat->capacity_nominal_wh) * 100.0 < bat->min_operating_soc) {
        discharge_power *= 0.5;
    }

    bat->total_discharge_wh += discharge_power * (1.0 / 3600.0); /* placeholder */

    (void)discharge_power; /* actuator uses this */
}

/* Compute conservative max charge power (W) */
double battery_calculate_max_charge(battery_system_t* bat) {
    if (!bat) return 0.0;
    double max_p = bat->max_charge_power_w;
    /* reduce near top SOC */
    if (bat->soc_smoothed > 80.0) {
        double factor = fmax(0.05, (100.0 - bat->soc_smoothed) / 20.0);
        max_p *= factor;
    }
    /* temperature derate */
    if (bat->temperature_c > 45.0) max_p *= 0.5;
    if (bat->temperature_c < 0.0) max_p = 0.0;
    return max_p;
}

/* Compute conservative max discharge power (W) */
double battery_calculate_max_discharge(battery_system_t* bat) {
    if (!bat) return 0.0;
    double max_p = bat->max_discharge_power_w;
    if (bat->soc_smoothed < 30.0) {
        double factor = fmax(0.0, (bat->soc_smoothed - bat->min_operating_soc) / (30.0 - bat->min_operating_soc));
        max_p *= factor;
    }
    if (bat->temperature_c > 55.0) max_p *= 0.5;
    if (bat->temperature_c < -10.0) max_p *= 0.2;
    return max_p;
}

/* Check hardware & safety limits; return true if fault found */
bool battery_check_limits(battery_system_t* bat, system_measurements_t* measurements) {
    if (!bat || !measurements) return false;

    bool fault = false;
    bat->last_fault_reason[0] = '\0';

    /* Voltage per-cell sanity using configured series cells */
    int s = bat->banks[0].cells_in_series;
    if (s <= 0) s = DEFAULT_BANK_SERIES_CELLS;
    double cell_v = (s > 0) ? (measurements->battery_voltage / (double)s) : 0.0;

    /* chemistry-specific thresholds (LFP defaults) */
    double cell_v_max = 3.65, cell_v_min = 2.5;
    if (bat->chemistry == BAT_CHEM_NMC) { cell_v_max = 4.2; cell_v_min = 3.0; }
    if (bat->chemistry == BAT_CHEM_LEAD_ACID) { cell_v_max = 2.45; cell_v_min = 1.75; }

    if (cell_v > cell_v_max * 1.05) {
        bat->overvoltage_fault = true;
        strncpy(bat->last_fault_reason, "Cell overvoltage", sizeof(bat->last_fault_reason)-1);
        fault = true;
    }
    if (cell_v < cell_v_min * 0.95) {
        bat->undervoltage_fault = true;
        strncpy(bat->last_fault_reason, "Cell undervoltage", sizeof(bat->last_fault_reason)-1);
        fault = true;
    }

    /* Current limit (basic) */
    // double pack_v = bat->banks[0].nominal_voltage;
    double max_charge_a = bat->max_charge_current_a;
    double max_discharge_a = bat->max_discharge_current_a;

    if (measurements->battery_current > max_charge_a * 1.2) {
        bat->overcurrent_fault = true;
        strncpy(bat->last_fault_reason, "Charge overcurrent", sizeof(bat->last_fault_reason)-1);
        fault = true;
    }
    if (measurements->battery_current < -max_discharge_a * 1.2) {
        bat->overcurrent_fault = true;
        strncpy(bat->last_fault_reason, "Discharge overcurrent", sizeof(bat->last_fault_reason)-1);
        fault = true;
    }

    /* Temperature */
    if (measurements->battery_temp > 60.0) {
        bat->overtemperature_fault = true;
        strncpy(bat->last_fault_reason, "Overtemperature", sizeof(bat->last_fault_reason)-1);
        fault = true;
    }

    if (fault) {
        bat->state = BATTERY_STATE_FAULT;
    }

    return fault;
}

/* Simple thermal manager with hysteresis */
void battery_thermal_management(battery_system_t* bat) {
    if (!bat) return;

    const double cooling_on = 35.0;
    const double cooling_off = 33.0;
    const double heating_on = 8.0;
    const double heating_off = 10.0;

    if (bat->temperature_c >= cooling_on) {
        bat->cooling_active = true;
    } else if (bat->temperature_c <= cooling_off) {
        bat->cooling_active = false;
    }

    if (bat->temperature_c <= heating_on) {
        bat->heating_active = true;
    } else if (bat->temperature_c >= heating_off) {
        bat->heating_active = false;
    }
}

/* Equalize routine (only invoked for chemistries that support it) */
void battery_equalize(battery_system_t* bat) {
    if (!bat) return;

    if (bat->chemistry == BAT_CHEM_LEAD_ACID) {
        /* perform lead-acid equalization if enabled and near full */
        if (bat->soc_smoothed > 95.0) {
            bat->state = BATTERY_STATE_CHARGING;
            bat->charge_stage = CHARGE_EQUALIZE;
            /* equalize logic handled by charger hardware/system */
        }
    }
}

/* Pretty-print battery status */
void battery_log_status(const battery_system_t* bat) {
    if (!bat) return;

    printf("=== Battery System Status ===\n");
    printf("State: %d (%s)\n", bat->state,
           (bat->state == BATTERY_STATE_CHARGING) ? "CHARGING" :
           (bat->state == BATTERY_STATE_DISCHARGING) ? "DISCHARGING" :
           (bat->state == BATTERY_STATE_FLOAT) ? "FLOAT" :
           (bat->state == BATTERY_STATE_EQUALIZE) ? "EQUALIZE" :
           (bat->state == BATTERY_STATE_FAULT) ? "FAULT" : "IDLE");
    printf("Charge Stage: %d\n", bat->charge_stage);
    printf("SOC: %.2f%% (est: %.2f, coulomb: %.2f, voltage: %.2f)\n",
           bat->soc_smoothed, bat->soc_estimated, bat->soc_coulomb, bat->soc_voltage);
    printf("Capacity Remaining: %.0f Wh / %.0f Wh nominal\n", bat->capacity_remaining_wh, bat->capacity_nominal_wh);
    printf("Temp: %.1f °C (ambient %.1f °C) Cooling=%s Heating=%s\n",
           bat->temperature_c, bat->ambient_temperature_c,
           bat->cooling_active ? "ON" : "OFF",
           bat->heating_active ? "ON" : "OFF");
    if (bat->state == BATTERY_STATE_FAULT) {
        printf("FAULT: %s\n", bat->last_fault_reason);
    }
    printf("Total Charge: %.3f kWh, Total Discharge: %.3f kWh\n",
           bat->total_charge_wh / 1000.0, bat->total_discharge_wh / 1000.0);
    printf("Cycle Count: %d\n", bat->cycle_count);
    printf("============================\n");
}
