/*
 * pbt.h - a small property-based testing harness (Hypothesis-style).
 *
 * A property draws its inputs from a choice sequence. Generation fills the
 * sequence from a seeded PRNG; on failure the harness shrinks the sequence
 * (delete choices, zero them, halve them, decrement them) while the property
 * still fails, then prints the minimal counterexample and a replay command.
 *
 *   PBT_RUNS=<n>   number of generated cases per property (default 500)
 *   PBT_SEED=<s>   base seed (default fixed, so gating runs are repeatable);
 *                  case i uses seed s+i, so `PBT_SEED=<case seed> PBT_RUNS=1`
 *                  replays a reported failure.
 */
#ifndef PBT_H
#define PBT_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PBT_MAX_CHOICES 4096
#define PBT_SHRINK_BUDGET 20000

typedef struct {
    uint32_t buf[PBT_MAX_CHOICES];
    uint32_t len;
    uint32_t pos;
    bool replay;
    uint64_t rng;
    char msg[512];
    char log[2048];   /* drawn values, for the counterexample report */
    size_t loglen;
    bool logging;
} pbt_t;

typedef bool (*pbt_prop_t)(pbt_t *p);

static inline uint64_t pbt_next(uint64_t *s)
{
    uint64_t z = (*s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static inline void pbt_note(pbt_t *p, const char *fmt, ...)
{
    if (!p->logging || p->loglen >= sizeof p->log - 1) return;
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(p->log + p->loglen, sizeof p->log - p->loglen, fmt, ap);
    va_end(ap);
    if (n > 0) p->loglen += (size_t)n;
    if (p->loglen >= sizeof p->log) p->loglen = sizeof p->log - 1;
}

/* Draw an integer in [0, max]. */
static inline uint32_t pbt_draw(pbt_t *p, uint32_t max)
{
    uint32_t v;
    if (p->replay) {
        v = (p->pos < p->len) ? p->buf[p->pos] : 0u;
        if (v > max) v = max;
    } else {
        uint64_t r = pbt_next(&p->rng);
        /* bias toward edge values: 1/8 min, 1/8 max */
        uint32_t pick = (uint32_t)(r & 7u);
        if (pick == 0) v = 0;
        else if (pick == 1) v = max;
        else v = (max == UINT32_MAX) ? (uint32_t)(r >> 32) : (uint32_t)((r >> 32) % ((uint64_t)max + 1u));
        if (p->pos < PBT_MAX_CHOICES) p->buf[p->pos] = v;
    }
    p->pos++;
    if (!p->replay && p->pos <= PBT_MAX_CHOICES) p->len = p->pos;
    return v;
}

static inline int32_t pbt_range(pbt_t *p, int32_t lo, int32_t hi)
{
    return lo + (int32_t)pbt_draw(p, (uint32_t)(hi - lo));
}

static inline bool pbt_bool(pbt_t *p) { return pbt_draw(p, 1u) != 0u; }

static inline bool pbt_fail(pbt_t *p, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    vsnprintf(p->msg, sizeof p->msg, fmt, ap);
    va_end(ap);
    return false;
}

static inline bool pbt_run_replay(pbt_t *p, pbt_prop_t prop, const uint32_t *buf, uint32_t len)
{
    memcpy(p->buf, buf, len * sizeof buf[0]);
    p->len = len; p->pos = 0; p->replay = true; p->msg[0] = '\0';
    p->loglen = 0; p->log[0] = '\0';
    return prop(p);
}

static uint32_t pbt_work[PBT_MAX_CHOICES];
static uint32_t pbt_best[PBT_MAX_CHOICES];

/* Shrink pbt_best[0..*len) while the property keeps failing. */
static inline void pbt_shrink(pbt_t *p, pbt_prop_t prop, uint32_t *len)
{
    int budget = PBT_SHRINK_BUDGET;
    bool improved = true;
    while (improved && budget > 0) {
        improved = false;
        for (uint32_t i = 0; i < *len && budget > 0; i++) {
            /* 1. delete choice i */
            if (*len > 1) {
                memcpy(pbt_work, pbt_best, i * sizeof(uint32_t));
                memcpy(pbt_work + i, pbt_best + i + 1, (*len - i - 1) * sizeof(uint32_t));
                budget--;
                if (!pbt_run_replay(p, prop, pbt_work, *len - 1)) {
                    (*len)--; memcpy(pbt_best, pbt_work, *len * sizeof(uint32_t));
                    improved = true; i--; continue;
                }
            }
            /* 2. smaller values: 0, half, minus one */
            uint32_t cands[3] = { 0u, pbt_best[i] / 2u, pbt_best[i] ? pbt_best[i] - 1u : 0u };
            for (int c = 0; c < 3 && budget > 0; c++) {
                if (cands[c] >= pbt_best[i]) continue;
                memcpy(pbt_work, pbt_best, *len * sizeof(uint32_t));
                pbt_work[i] = cands[c];
                budget--;
                if (!pbt_run_replay(p, prop, pbt_work, *len)) {
                    pbt_best[i] = cands[c]; improved = true; break;
                }
            }
        }
    }
}

static inline int pbt_check(const char *name, pbt_prop_t prop)
{
    static pbt_t p;
    const char *runs_env = getenv("PBT_RUNS"), *seed_env = getenv("PBT_SEED");
    uint32_t runs = runs_env ? (uint32_t)strtoul(runs_env, NULL, 0) : 500u;
    uint64_t base = seed_env ? strtoull(seed_env, NULL, 0) : 0x6A6D5EEDull;
    for (uint32_t i = 0; i < runs; i++) {
        uint64_t seed = base + i;
        memset(&p, 0, sizeof p);
        p.rng = seed;
        if (prop(&p)) continue;
        uint32_t len = p.len;
        char first[512]; snprintf(first, sizeof first, "%s", p.msg);
        memcpy(pbt_best, p.buf, len * sizeof(uint32_t));
        pbt_shrink(&p, prop, &len);
        p.logging = true;
        pbt_run_replay(&p, prop, pbt_best, len);
        printf("FALSIFIED %s after %u case(s)\n  original: %s\n  shrunk:   %s\n  inputs:   %s\n"
               "  replay:   PBT_SEED=%llu PBT_RUNS=1 (property %s)\n",
               name, i + 1, first, p.msg, p.log, (unsigned long long)seed, name);
        return 1;
    }
    printf("%s: %u cases passed\n", name, runs);
    return 0;
}

#endif
