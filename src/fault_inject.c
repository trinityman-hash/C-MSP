/* fault_inject.c
 *
 * Deterministic, table-backed fault registry. Only built when
 * CONFIG_FAULT_INJECTION is defined -- see fault_inject.h for why the
 * header makes FI_POINT vanish entirely otherwise.
 *
 * Under Zephyr (__ZEPHYR__ defined by the build), all table access is
 * protected by a single spinlock: a fault point can legitimately be hit
 * from a driver ISR or a different thread than the one arming/disarming
 * it, and torn reads/writes to a shared struct are a real bug, not a
 * theoretical one, the moment this runs under a real scheduler instead
 * of a single-threaded host test.
 */

#ifdef CONFIG_FAULT_INJECTION

#include "fault_inject.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __ZEPHYR__
#include <zephyr/spinlock.h>
static struct k_spinlock fi_lock;
#define FI_LOCK(key)   k_spinlock_key_t key = k_spin_lock(&fi_lock)
#define FI_UNLOCK(key) k_spin_unlock(&fi_lock, key)
#else
#define FI_LOCK(key)   (void)0
#define FI_UNLOCK(key) (void)0
#endif

struct fi_entry {
    uint32_t id;
    bool in_use;
    bool armed;
    int inject_value;
    uint32_t hit_count;
};

static struct fi_entry fi_table[FI_MAX_POINTS];

/* Both helpers assume the caller already holds fi_lock. */

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
    FI_LOCK(key);
    struct fi_entry *e = fi_lookup_or_create(fault_id);
    if (e == NULL) {
        FI_UNLOCK(key);
        return 0;
    }
    e->hit_count++;
    int rc = e->armed ? e->inject_value : 0;
    FI_UNLOCK(key);
    return rc;
}

void fi_arm(uint32_t fault_id, int inject_value)
{
    FI_LOCK(key);
    struct fi_entry *e = fi_lookup_or_create(fault_id);
    if (e != NULL) {
        e->armed = true;
        e->inject_value = inject_value;
    }
    FI_UNLOCK(key);
}

void fi_disarm(uint32_t fault_id)
{
    FI_LOCK(key);
    struct fi_entry *e = fi_lookup(fault_id);
    if (e != NULL) {
        e->armed = false;
    }
    FI_UNLOCK(key);
}

void fi_reset_all(void)
{
    FI_LOCK(key);
    for (int i = 0; i < FI_MAX_POINTS; i++) {
        fi_table[i].in_use = false;
        fi_table[i].armed = false;
        fi_table[i].inject_value = 0;
        fi_table[i].hit_count = 0;
        fi_table[i].id = 0;
    }
    FI_UNLOCK(key);
}

uint32_t fi_hit_count(uint32_t fault_id)
{
    FI_LOCK(key);
    struct fi_entry *e = fi_lookup(fault_id);
    uint32_t count = e != NULL ? e->hit_count : 0;
    FI_UNLOCK(key);
    return count;
}

#endif /* CONFIG_FAULT_INJECTION */
