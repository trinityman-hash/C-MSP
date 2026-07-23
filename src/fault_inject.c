/* fault_inject.c
 *
 * Deterministic, table-backed fault registry. Only built when
 * FAULT_INJECTION_ENABLED is defined -- see fault_inject.h for why the
 * header makes FI_POINT vanish entirely otherwise.
 */

#ifdef FAULT_INJECTION_ENABLED

#include "fault_inject.h"
#include <stdbool.h>
#include <stddef.h>

struct fi_entry {
    uint32_t id;
    bool in_use;
    bool armed;
    int inject_value;
    uint32_t hit_count;
};

static struct fi_entry fi_table[FI_MAX_POINTS];

static struct fi_entry *fi_lookup(uint32_t fault_id)
{
    for (int i = 0; i < FI_MAX_POINTS; i++) {
        if (fi_table[i].in_use && fi_table[i].id == fault_id) {
            return &fi_table[i];
        }
    }
    return NULL;
}

static struct fi_entry *fi_lookup_or_create(uint32_t fault_id)
{
    struct fi_entry *e = fi_lookup(fault_id);
    if (e != NULL) {
        return e;
    }
    for (int i = 0; i < FI_MAX_POINTS; i++) {
        if (!fi_table[i].in_use) {
            fi_table[i].in_use = true;
            fi_table[i].id = fault_id;
            fi_table[i].armed = false;
            fi_table[i].inject_value = 0;
            fi_table[i].hit_count = 0;
            return &fi_table[i];
        }
    }
    return NULL; /* table full; caller treats as "not armed" */
}

int fi_should_fail(uint32_t fault_id)
{
    struct fi_entry *e = fi_lookup_or_create(fault_id);
    if (e == NULL) {
        return 0;
    }
    e->hit_count++;
    if (!e->armed) {
        return 0;
    }
    return e->inject_value;
}

void fi_arm(uint32_t fault_id, int inject_value)
{
    struct fi_entry *e = fi_lookup_or_create(fault_id);
    if (e == NULL) {
        return;
    }
    e->armed = true;
    e->inject_value = inject_value;
}

void fi_disarm(uint32_t fault_id)
{
    struct fi_entry *e = fi_lookup(fault_id);
    if (e != NULL) {
        e->armed = false;
    }
}

void fi_reset_all(void)
{
    for (int i = 0; i < FI_MAX_POINTS; i++) {
        fi_table[i].in_use = false;
        fi_table[i].armed = false;
        fi_table[i].inject_value = 0;
        fi_table[i].hit_count = 0;
        fi_table[i].id = 0;
    }
}

uint32_t fi_hit_count(uint32_t fault_id)
{
    struct fi_entry *e = fi_lookup(fault_id);
    return e != NULL ? e->hit_count : 0;
}

#endif /* FAULT_INJECTION_ENABLED */
