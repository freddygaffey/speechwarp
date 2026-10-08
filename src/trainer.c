/* speechwarp - listener trainer and blind trials.
 *
 * Licensed under the Apache License, Version 2.0. See LICENSE and NOTICE.
 *
 * Pure logic, no audio: the caller passes scores, rates and its own timestamps, and gets rates, plans and
 * trial designs back. Everything random comes from a seeded generator, so the same calls give the same
 * answers. The design and the research behind it are in docs/research-high-speed.md.
 *
 * The parts:
 *
 *   a. the threshold test: the psi method over a grid of thresholds and slopes;
 *   b. session plans: steady, ramp, interval and tracking;
 *   c. comparing plans: Thompson sampling over a Bayesian linear model of threshold gain per hour;
 *   d. blind trials: which setting and values to compare next, and when one value reliably wins.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "speechwarp.h"

/* ---- Random numbers ---------------------------------------------------------------------------------- */

/* SplitMix64 (Steele, Lea and Flood, 2014): small, fast and good enough for choosing and sampling. */
typedef struct {
  uint64_t state;
} rng;

static uint64_t rng_next(rng* r) {
  uint64_t z = (r->state += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

/* Uniform in (0, 1). */
static double rng_uniform(rng* r) { return ((double)(rng_next(r) >> 11) + 0.5) * (1.0 / 9007199254740992.0); }

static int rng_below(rng* r, int n) { return (int)(rng_uniform(r) * n); }

static double rng_normal(rng* r) {
  double u = rng_uniform(r), v = rng_uniform(r);
  return sqrt(-2 * log(u)) * cos(2 * 3.141592653589793 * v);
}

/* Gamma(shape, 1), by Marsaglia and Tsang (2000); shapes below 1 are boosted by u^(1/shape). */
static double rng_gamma(rng* r, double shape) {
  double d, c;
  if (shape < 1) {
    return rng_gamma(r, shape + 1) * pow(rng_uniform(r), 1 / shape);
  }
  d = shape - 1.0 / 3;
  c = 1 / sqrt(9 * d);
  for (;;) {
    double x = rng_normal(r), v = 1 + c * x, u;
    if (v <= 0) {
      continue;
    }
    v = v * v * v;
    u = rng_uniform(r);
    if (log(u) < 0.5 * x * x + d - d * v + d * log(v)) {
      return d * v;
    }
  }
}

static int is_finite(double x) { return x == x && x - x == 0; }

/* ---- The trainer ------------------------------------------------------------------------------------- */

/* The grid the threshold posterior lives on: thresholds from 3 to 60 syllables a second (log spaced), and
 * slopes of the psychometric function in ln(rate), log spaced. The test presents rates from the same range. */
#define GRID_THRESHOLDS 121
#define GRID_SLOPES 12
#define MIN_RATE 3.0
#define MAX_RATE 60.0
#define MIN_SLOPE 2.0
#define MAX_SLOPE 30.0
#define CANDIDATES 58
/* The share of answers wrong whatever the rate: a word misheard, a slip of attention. */
#define LAPSE 0.04
#define TEST_MINIMUM 8

/* Priors. Without a previous estimate, about 10 syllables a second (an untrained listener follows about 8,
 * trained blind listeners up to 22), broad; with one, around it. */
#define DEFAULT_PRIOR_RATE 10.0
#define DEFAULT_PRIOR_SD 0.6
#define KNOWN_PRIOR_SD 0.35

/* Comparing plans (see that section). Normal-inverse-gamma prior: noise variance about 0.02 (a threshold
 * measured twice differs by about 14%), plan effects about 0 +- 0.1 a hour at first. */
#define FEATURES SPEECHWARP_PLAN_COUNT
#define NOISE_A0 2.0
#define NOISE_B0 0.02
#define EFFECT_V0 0.5 /* x the noise variance: sd 0.1 */
/* Retention: each plan's deviation from the pooled mean at the same delay, shrunk towards none by this many
 * items' worth of prior. A day or less is one delay band, longer the other. */
#define RETENTION_PRIOR_ITEMS 2.0
#define RETENTION_DAY (36 * 3600.0)
#define BEST_DRAWS 4000

typedef struct {
  int plan;
  double before, after, hours, hours_before;
  int valid; /* a threshold test ended after the session began, and some listening was done */
} session;

typedef struct {
  int session;
  double score, items; /* items already weighted */
  double delay;
} retention;

struct speechwarp_trainer {
  rng random;
  uint64_t seed;
  double weight[4];
  double param[SPEECHWARP_PARAM_COUNT];

  double ln_threshold[GRID_THRESHOLDS];
  double slope[GRID_SLOPES];
  double candidate[CANDIDATES]; /* ln rates */

  /* The threshold test. */
  int testing;
  int presentations;
  double posterior[GRID_THRESHOLDS * GRID_SLOPES]; /* log density, up to a constant */
  double threshold, threshold_low, threshold_high; /* the last finished estimate */
  int have_threshold;
  double last_test_end;
  int have_test_end;

  /* The session. */
  int in_session;
  int plan;
  double session_start;
  double base;      /* threshold at session_begin */
  int base_measured;
  double tracking;  /* ln rate, for the tracking plan */

  session* sessions;
  int session_count, session_capacity;
  double hours_total;
  retention* retentions;
  int retention_count, retention_capacity;
};

static const double default_param[SPEECHWARP_PARAM_COUNT] = {0.75, 0.10, 0.8, 0.02, 2, 0.15, 10, 0.4, 0.2, 40, 1.25};
static const double param_min[SPEECHWARP_PARAM_COUNT] = {0.5, -0.5, 0.3, 0.001, 0.1, 0, 0.5, 0, 0, TEST_MINIMUM, 1.01};
static const double param_max[SPEECHWARP_PARAM_COUNT] = {0.95, 1, 1, 0.5, 60, 0.9, 120, 5, 100, 1000, 4};
static const double guess[4] = {0, 0.5, 0.5, 0};

speechwarp_trainer* speechwarp_trainer_create(uint64_t seed) {
  speechwarp_trainer* t = (speechwarp_trainer*)calloc(1, sizeof(*t));
  int i;
  if (!t) {
    return NULL;
  }
  t->seed = seed;
  t->random.state = seed;
  t->weight[SPEECHWARP_MEASURE_INTELLIGIBILITY] = 0.5;
  t->weight[SPEECHWARP_MEASURE_VERIFICATION] = 1;
  t->weight[SPEECHWARP_MEASURE_RETENTION] = 1;
  t->weight[SPEECHWARP_MEASURE_RATING] = 0.3;
  memcpy(t->param, default_param, sizeof(default_param));
  for (i = 0; i < GRID_THRESHOLDS; i++) {
    t->ln_threshold[i] = log(MIN_RATE) + (log(MAX_RATE) - log(MIN_RATE)) * i / (GRID_THRESHOLDS - 1);
  }
  for (i = 0; i < GRID_SLOPES; i++) {
    t->slope[i] = MIN_SLOPE * pow(MAX_SLOPE / MIN_SLOPE, (double)i / (GRID_SLOPES - 1));
  }
  for (i = 0; i < CANDIDATES; i++) {
    t->candidate[i] = log(MIN_RATE) + (log(MAX_RATE) - log(MIN_RATE)) * i / (CANDIDATES - 1);
  }
  return t;
}

void speechwarp_trainer_destroy(speechwarp_trainer* trainer) {
  if (!trainer) {
    return;
  }
  free(trainer->sessions);
  free(trainer->retentions);
  free(trainer);
}

void speechwarp_trainer_set_weight(speechwarp_trainer* trainer, int kind, double weight) {
  if (trainer && kind >= 0 && kind < 4 && weight >= 0 && is_finite(weight)) {
    trainer->weight[kind] = weight;
  }
}

double speechwarp_trainer_get_weight(const speechwarp_trainer* trainer, int kind) {
  return trainer && kind >= 0 && kind < 4 ? trainer->weight[kind] : 0;
}

void speechwarp_trainer_set_param(speechwarp_trainer* trainer, int param, double value) {
  if (!trainer || param < 0 || param >= SPEECHWARP_PARAM_COUNT || !is_finite(value)) {
    return;
  }
  if (value < param_min[param]) value = param_min[param];
  if (value > param_max[param]) value = param_max[param];
  trainer->param[param] = value;
}

double speechwarp_trainer_get_param(const speechwarp_trainer* trainer, int param) {
  return trainer && param >= 0 && param < SPEECHWARP_PARAM_COUNT ? trainer->param[param] : 0;
}

/* ---- a. The threshold test ----------------------------------------------------------------------------
 *
 * The share of a sentence understood at rate r, for a listener with threshold T and slope b, is a logistic
 * in ln r that falls through TARGET at T:
 *
 *     u(r) = 1 / (1 + exp(b (ln r - ln T) - logit(TARGET)))
 *
 * and the chance of a right answer to an item of a kind with guess rate g is g + (1 - g - LAPSE) u(r). The
 * posterior over (T, b) is kept on the grid. Each presentation goes to the candidate rate that minimises the
 * expected entropy of the threshold's marginal posterior, treating the slope as a nuisance (psi-marginal,
 * Prins 2013), and assuming a one-item intelligibility answer. */

static double understood(const speechwarp_trainer* t, double ln_rate, double ln_threshold, double slope) {
  double target = t->param[SPEECHWARP_PARAM_TARGET];
  return 1 / (1 + exp(slope * (ln_rate - ln_threshold) - log(target / (1 - target))));
}

static double p_right(const speechwarp_trainer* t, int kind, double ln_rate, double ln_threshold, double slope) {
  double p = guess[kind] + (1 - guess[kind] - LAPSE) * understood(t, ln_rate, ln_threshold, slope);
  if (p < 1e-9) p = 1e-9;
  if (p > 1 - 1e-9) p = 1 - 1e-9;
  return p;
}

static void normalise_log(double* values, int count) {
  double most = -HUGE_VAL;
  int i;
  for (i = 0; i < count; i++) {
    if (values[i] > most) most = values[i];
  }
  for (i = 0; i < count; i++) {
    values[i] -= most;
  }
}

/* The threshold's marginal posterior, normalised to sum to 1. */
static void marginal(const speechwarp_trainer* t, const double* log_posterior, double* out) {
  double total = 0;
  int i, j;
  for (i = 0; i < GRID_THRESHOLDS; i++) {
    double sum = 0;
    for (j = 0; j < GRID_SLOPES; j++) {
      sum += exp(log_posterior[i * GRID_SLOPES + j]);
    }
    out[i] = sum;
    total += sum;
  }
  for (i = 0; i < GRID_THRESHOLDS; i++) {
    out[i] /= total;
  }
  (void)t;
}

/* The rate at which the marginal reaches `share`, interpolated in ln rate. */
static double quantile(const speechwarp_trainer* t, const double* mass, double share) {
  double below = 0;
  int i;
  for (i = 0; i < GRID_THRESHOLDS; i++) {
    if (below + mass[i] >= share) {
      /* Treat each grid point as owning the interval around it. */
      double step = t->ln_threshold[1] - t->ln_threshold[0];
      double into = mass[i] > 0 ? (share - below) / mass[i] : 0.5;
      return exp(t->ln_threshold[i] - step / 2 + step * into);
    }
    below += mass[i];
  }
  return exp(t->ln_threshold[GRID_THRESHOLDS - 1]);
}

static void summarise(const speechwarp_trainer* t, double* median, double* low, double* high) {
  double mass[GRID_THRESHOLDS];
  marginal(t, t->posterior, mass);
  *median = quantile(t, mass, 0.5);
  *low = quantile(t, mass, 0.025);
  *high = quantile(t, mass, 0.975);
}

void speechwarp_trainer_test_begin(speechwarp_trainer* trainer, double prior_rate, double time) {
  double centre, sd;
  int i, j;
  if (!trainer || !is_finite(prior_rate) || !is_finite(time)) {
    return;
  }
  if (prior_rate > 0) {
    centre = log(prior_rate);
    sd = KNOWN_PRIOR_SD;
  } else if (trainer->have_threshold) {
    centre = log(trainer->threshold);
    sd = KNOWN_PRIOR_SD;
  } else {
    centre = log(DEFAULT_PRIOR_RATE);
    sd = DEFAULT_PRIOR_SD;
  }
  /* Log-normal in the threshold, flat in ln(slope). */
  for (i = 0; i < GRID_THRESHOLDS; i++) {
    double z = (trainer->ln_threshold[i] - centre) / sd;
    for (j = 0; j < GRID_SLOPES; j++) {
      trainer->posterior[i * GRID_SLOPES + j] = -0.5 * z * z;
    }
  }
  trainer->testing = 1;
  trainer->presentations = 0;
}

/* A score from `items` weighted items counts as that many Bernoulli trials, `score` of them right. */
static void test_update(speechwarp_trainer* t, int kind, double score, double items, double ln_rate) {
  int i, j;
  for (i = 0; i < GRID_THRESHOLDS; i++) {
    for (j = 0; j < GRID_SLOPES; j++) {
      double p = p_right(t, kind, ln_rate, t->ln_threshold[i], t->slope[j]);
      t->posterior[i * GRID_SLOPES + j] += items * (score * log(p) + (1 - score) * log(1 - p));
    }
  }
  normalise_log(t->posterior, GRID_THRESHOLDS * GRID_SLOPES);
}

static double entropy(const double* mass) {
  double h = 0;
  int i;
  for (i = 0; i < GRID_THRESHOLDS; i++) {
    if (mass[i] > 0) {
      h -= mass[i] * log(mass[i]);
    }
  }
  return h;
}

double speechwarp_trainer_test_rate(speechwarp_trainer* trainer) {
  double best_rate = 0, best_entropy = HUGE_VAL;
  double density[GRID_THRESHOLDS * GRID_SLOPES];
  double right[GRID_THRESHOLDS * GRID_SLOPES], wrong[GRID_THRESHOLDS * GRID_SLOPES];
  double total = 0;
  int c, k;
  const int cells = GRID_THRESHOLDS * GRID_SLOPES;

  if (!trainer || !trainer->testing) {
    return 0;
  }
  for (k = 0; k < cells; k++) {
    density[k] = exp(trainer->posterior[k]);
    total += density[k];
  }
  for (k = 0; k < cells; k++) {
    density[k] /= total;
  }
  for (c = 0; c < CANDIDATES; c++) {
    double p_r = 0, mass_r[GRID_THRESHOLDS], mass_w[GRID_THRESHOLDS], expected;
    int i, j;
    for (i = 0; i < GRID_THRESHOLDS; i++) {
      double sum_r = 0, sum_w = 0;
      for (j = 0; j < GRID_SLOPES; j++) {
        int at = i * GRID_SLOPES + j;
        double p = p_right(trainer, SPEECHWARP_MEASURE_INTELLIGIBILITY, trainer->candidate[c],
                           trainer->ln_threshold[i], trainer->slope[j]);
        right[at] = density[at] * p;
        wrong[at] = density[at] * (1 - p);
        sum_r += right[at];
        sum_w += wrong[at];
      }
      mass_r[i] = sum_r;
      mass_w[i] = sum_w;
      p_r += sum_r;
    }
    for (i = 0; i < GRID_THRESHOLDS; i++) {
      mass_r[i] /= p_r;
      mass_w[i] /= 1 - p_r;
    }
    expected = p_r * entropy(mass_r) + (1 - p_r) * entropy(mass_w);
    if (expected < best_entropy - 1e-12) {
      best_entropy = expected;
      best_rate = exp(trainer->candidate[c]);
    }
  }
  return best_rate;
}

int speechwarp_trainer_test_done(const speechwarp_trainer* trainer) {
  double median, low, high;
  if (!trainer || !trainer->testing) {
    return 0;
  }
  if (trainer->presentations >= (int)trainer->param[SPEECHWARP_PARAM_TEST_MAX]) {
    return 1;
  }
  if (trainer->presentations < TEST_MINIMUM) {
    return 0;
  }
  summarise(trainer, &median, &low, &high);
  return high / low < trainer->param[SPEECHWARP_PARAM_TEST_PRECISION];
}

double speechwarp_trainer_test_end(speechwarp_trainer* trainer, double time) {
  if (!trainer || !trainer->testing || !is_finite(time)) {
    return 0;
  }
  summarise(trainer, &trainer->threshold, &trainer->threshold_low, &trainer->threshold_high);
  trainer->have_threshold = 1;
  trainer->testing = 0;
  trainer->last_test_end = time;
  trainer->have_test_end = 1;
  return trainer->threshold;
}

double speechwarp_trainer_threshold(const speechwarp_trainer* trainer) {
  double median, low, high;
  if (!trainer) return 0;
  if (trainer->testing) {
    summarise(trainer, &median, &low, &high);
    return median;
  }
  return trainer->have_threshold ? trainer->threshold : 0;
}

double speechwarp_trainer_threshold_low(const speechwarp_trainer* trainer) {
  double median, low, high;
  if (!trainer) return 0;
  if (trainer->testing) {
    summarise(trainer, &median, &low, &high);
    return low;
  }
  return trainer->have_threshold ? trainer->threshold_low : 0;
}

double speechwarp_trainer_threshold_high(const speechwarp_trainer* trainer) {
  double median, low, high;
  if (!trainer) return 0;
  if (trainer->testing) {
    summarise(trainer, &median, &low, &high);
    return high;
  }
  return trainer->have_threshold ? trainer->threshold_high : 0;
}

/* ---- b. Sessions -------------------------------------------------------------------------------------- */

static double clamp_rate(double rate) {
  if (rate < 1) return 1;
  if (rate > 100) return 100;
  return rate;
}

/* The share understood, from a score: the guess rate and lapses taken out. */
static double share_understood(int kind, double score) {
  double u = (score - guess[kind]) / (1 - guess[kind] - LAPSE);
  if (u < 0) u = 0;
  if (u > 1) u = 1;
  return u;
}

int speechwarp_trainer_add_measure(speechwarp_trainer* trainer, int kind, double score, double items,
                                   double rate, double time) {
  double weighted;
  if (!trainer || (kind != SPEECHWARP_MEASURE_INTELLIGIBILITY && kind != SPEECHWARP_MEASURE_VERIFICATION &&
                   kind != SPEECHWARP_MEASURE_RATING) ||
      !(score >= 0 && score <= 1) || !(items > 0) || !is_finite(items) || !(rate > 0) || !is_finite(rate) ||
      !is_finite(time)) {
    return 0;
  }
  weighted = items * trainer->weight[kind];
  if (trainer->testing) {
    if (weighted > 0) {
      test_update(trainer, kind, score, weighted, log(rate));
    }
    trainer->presentations++;
  } else if (trainer->in_session && trainer->plan == SPEECHWARP_PLAN_TRACKING) {
    /* A weighted up-down rule in continuous form (after Kaernbach, 1991): up a little after a good check,
     * down three times as much after a bad one at TARGET 0.75, so the rate settles where the expected share
     * understood is TARGET. Measured from the rate heard, so a check taken late does not undo a later step. */
    double strength = trainer->weight[kind] < 1 ? trainer->weight[kind] : 1;
    double step = trainer->param[SPEECHWARP_PARAM_TRACKING_GAIN] * strength *
                  (share_understood(kind, score) - trainer->param[SPEECHWARP_PARAM_TARGET]);
    trainer->tracking = log(rate) + step;
  }
  return 1;
}

void speechwarp_trainer_session_begin(speechwarp_trainer* trainer, int plan, double time) {
  if (!trainer || plan < 0 || plan >= SPEECHWARP_PLAN_COUNT || !is_finite(time)) {
    return;
  }
  trainer->in_session = 1;
  trainer->plan = plan;
  trainer->session_start = time;
  trainer->base_measured = trainer->have_threshold;
  trainer->base = trainer->have_threshold ? trainer->threshold : DEFAULT_PRIOR_RATE;
  trainer->tracking = log(trainer->base);
}

double speechwarp_trainer_session_rate(speechwarp_trainer* trainer, double time) {
  const double* p;
  double minutes, rate = 0;
  if (!trainer || !trainer->in_session || !is_finite(time)) {
    return 0;
  }
  p = trainer->param;
  minutes = (time - trainer->session_start) / 60;
  if (minutes < 0) minutes = 0;
  switch (trainer->plan) {
    case SPEECHWARP_PLAN_STEADY:
      rate = trainer->base * (1 + p[SPEECHWARP_PARAM_MARGIN]);
      break;
    case SPEECHWARP_PLAN_RAMP: {
      double target = trainer->base * (1 + p[SPEECHWARP_PARAM_MARGIN]);
      double steps = floor(minutes / p[SPEECHWARP_PARAM_RAMP_MINUTES]);
      rate = target * (p[SPEECHWARP_PARAM_RAMP_START] + steps * p[SPEECHWARP_PARAM_RAMP_STEP]);
      if (rate > target) rate = target;
      break;
    }
    case SPEECHWARP_PLAN_INTERVAL: {
      long period = (long)floor(minutes / p[SPEECHWARP_PARAM_INTERVAL_MINUTES]);
      double spread = p[SPEECHWARP_PARAM_INTERVAL_SPREAD];
      rate = trainer->base * (period % 2 == 0 ? 1 + spread : 1 - spread);
      break;
    }
    case SPEECHWARP_PLAN_TRACKING:
      rate = exp(trainer->tracking);
      break;
  }
  return clamp_rate(rate);
}

int speechwarp_trainer_session_end(speechwarp_trainer* trainer, double listening_hours, double time) {
  session* s;
  if (!trainer || !trainer->in_session || !is_finite(listening_hours) || !is_finite(time)) {
    return -1;
  }
  if (trainer->session_count == trainer->session_capacity) {
    int capacity = trainer->session_capacity ? trainer->session_capacity * 2 : 16;
    session* grown = (session*)realloc(trainer->sessions, (size_t)capacity * sizeof(session));
    if (!grown) {
      return -1;
    }
    trainer->sessions = grown;
    trainer->session_capacity = capacity;
  }
  if (listening_hours < 0) listening_hours = 0;
  s = &trainer->sessions[trainer->session_count];
  s->plan = trainer->plan;
  s->before = trainer->base;
  s->after = trainer->have_threshold ? trainer->threshold : trainer->base;
  s->hours = listening_hours;
  s->hours_before = trainer->hours_total;
  s->valid = trainer->base_measured && trainer->have_test_end && trainer->last_test_end > trainer->session_start &&
             listening_hours > 0;
  trainer->hours_total += listening_hours;
  trainer->in_session = 0;
  return trainer->session_count++;
}

int speechwarp_trainer_add_retention(speechwarp_trainer* trainer, int session_number, double score, double items,
                                     double delay_seconds, double time) {
  retention* r;
  if (!trainer || session_number < 0 || session_number >= trainer->session_count || !(score >= 0 && score <= 1) ||
      !(items > 0) || !is_finite(items) || !(delay_seconds >= 0) || !is_finite(delay_seconds) || !is_finite(time)) {
    return 0;
  }
  if (trainer->retention_count == trainer->retention_capacity) {
    int capacity = trainer->retention_capacity ? trainer->retention_capacity * 2 : 16;
    retention* grown = (retention*)realloc(trainer->retentions, (size_t)capacity * sizeof(retention));
    if (!grown) {
      return 0;
    }
    trainer->retentions = grown;
    trainer->retention_capacity = capacity;
  }
  r = &trainer->retentions[trainer->retention_count++];
  r->session = session_number;
  r->score = score;
  r->items = items * trainer->weight[SPEECHWARP_MEASURE_RETENTION];
  r->delay = delay_seconds;
  return 1;
}

/* ---- c. Comparing plans --------------------------------------------------------------------------------
 *
 * Bayesian linear regression with a Normal-inverse-gamma prior (conjugate, so the posterior is exact). For a
 * session of h hours that began after c hours of listening in all,
 *
 *     ln(after / before) = h x effect[plan] / (1 + (c + h / 2) / H) + e,   e ~ N(0, s^2)
 *
 * Gains shrink as the listener improves (the power law of practice; Newell and Rosenbloom, 1981): by the time
 * H hours have been listened, a plan gains half what it did at first. Without this, whichever plan happened to
 * run early would look best. H is not known, so the model is fitted for each H on a grid and the fits are
 * averaged, weighted by their marginal likelihood. The noise is that of two threshold tests, so it does not
 * grow with the session's length, which is why the hours multiply the row rather than divide the outcome. */

#define HALVINGS 7
static const double halving_hours[HALVINGS] = {5, 10, 20, 40, 80, 160, 1e6}; /* the last: no slowing */

typedef struct {
  double mean[FEATURES];
  double cov[FEATURES][FEATURES]; /* V: the coefficients' covariance is s^2 V */
  double a, b;                    /* s^2 ~ InverseGamma(a, b) */
  double log_evidence;
  double slowing_now;             /* 1 / (1 + hours so far / H) */
} model;

typedef struct {
  model fits[HALVINGS];
  double weight[HALVINGS]; /* posterior probability of each H */
} models;

/* Cholesky factor L of a symmetric positive definite matrix, in the lower triangle. */
static int cholesky(double m[FEATURES][FEATURES], double l[FEATURES][FEATURES]) {
  int i, j, k;
  for (i = 0; i < FEATURES; i++) {
    for (j = 0; j <= i; j++) {
      double sum = m[i][j];
      for (k = 0; k < j; k++) {
        sum -= l[i][k] * l[j][k];
      }
      if (i == j) {
        if (sum <= 0) return 0;
        l[i][i] = sqrt(sum);
      } else {
        l[i][j] = sum / l[j][j];
      }
    }
    for (j = i + 1; j < FEATURES; j++) {
      l[i][j] = 0;
    }
  }
  return 1;
}

/* The inverse of a symmetric positive definite matrix and the log of its determinant, through its Cholesky
 * factor. */
static int invert(double m[FEATURES][FEATURES], double out[FEATURES][FEATURES], double* log_det) {
  double l[FEATURES][FEATURES], li[FEATURES][FEATURES];
  int i, j, k;
  if (!cholesky(m, l)) {
    return 0;
  }
  *log_det = 0;
  for (i = 0; i < FEATURES; i++) {
    *log_det += 2 * log(l[i][i]);
  }
  /* li = L^-1, lower triangular. */
  for (i = 0; i < FEATURES; i++) {
    for (j = 0; j < FEATURES; j++) {
      double sum = i == j ? 1 : 0;
      for (k = j; k < i; k++) {
        sum -= l[i][k] * li[k][j];
      }
      li[i][j] = j > i ? 0 : sum / l[i][i];
    }
  }
  for (i = 0; i < FEATURES; i++) {
    for (j = 0; j < FEATURES; j++) {
      double sum = 0;
      for (k = 0; k < FEATURES; k++) {
        sum += li[k][i] * li[k][j];
      }
      out[i][j] = sum;
    }
  }
  return 1;
}

static void fit(const speechwarp_trainer* t, double halving, model* m) {
  double precision[FEATURES][FEATURES], rhs[FEATURES];
  double yy = 0, fitted = 0, log_det_precision = 0;
  int n = 0, i, j, k;

  memset(precision, 0, sizeof(precision));
  memset(rhs, 0, sizeof(rhs));
  for (i = 0; i < FEATURES; i++) {
    precision[i][i] = 1 / EFFECT_V0; /* prior means are all 0 */
  }
  for (k = 0; k < t->session_count; k++) {
    const session* s = &t->sessions[k];
    double row, y;
    if (!s->valid) {
      continue;
    }
    row = s->hours / (1 + (s->hours_before + s->hours / 2) / halving);
    y = log(s->after / s->before);
    rhs[s->plan] += row * y;
    precision[s->plan][s->plan] += row * row;
    yy += y * y;
    n++;
  }
  if (!invert(precision, m->cov, &log_det_precision)) {
    memset(m->cov, 0, sizeof(m->cov));
    for (i = 0; i < FEATURES; i++) m->cov[i][i] = EFFECT_V0;
  }
  for (i = 0; i < FEATURES; i++) {
    double sum = 0;
    for (j = 0; j < FEATURES; j++) {
      sum += m->cov[i][j] * rhs[j];
    }
    m->mean[i] = sum;
    fitted += m->mean[i] * rhs[i]; /* mean' V^-1 mean, as V^-1 mean = rhs */
  }
  m->a = NOISE_A0 + n / 2.0;
  m->b = NOISE_B0 + (yy - fitted) / 2;
  if (m->b < NOISE_B0 / 10) m->b = NOISE_B0 / 10;
  /* log p(y | H), dropping the terms that are the same for every H. */
  m->log_evidence = -0.5 * log_det_precision - 0.5 * FEATURES * log(EFFECT_V0) + NOISE_A0 * log(NOISE_B0) -
                    m->a * log(m->b) + lgamma(m->a) - lgamma(NOISE_A0);
  m->slowing_now = 1 / (1 + t->hours_total / halving);
}

static void fit_all(const speechwarp_trainer* t, models* all) {
  double most = -HUGE_VAL, total = 0;
  int h;
  for (h = 0; h < HALVINGS; h++) {
    fit(t, halving_hours[h], &all->fits[h]);
    if (all->fits[h].log_evidence > most) most = all->fits[h].log_evidence;
  }
  for (h = 0; h < HALVINGS; h++) {
    all->weight[h] = exp(all->fits[h].log_evidence - most);
    total += all->weight[h];
  }
  for (h = 0; h < HALVINGS; h++) {
    all->weight[h] /= total;
  }
}

/* A plan's gain a hour at the current amount of practice, averaged over H: its mean and variance. */
static void gain_now(const models* all, int plan, double* mean, double* variance) {
  double m1 = 0, m2 = 0;
  int h;
  for (h = 0; h < HALVINGS; h++) {
    const model* m = &all->fits[h];
    double scale = m->b / (m->a > 1 ? m->a - 1 : 1); /* E[s^2] */
    double mu = m->mean[plan] * m->slowing_now;
    double var = scale * m->cov[plan][plan] * m->slowing_now * m->slowing_now;
    m1 += all->weight[h] * mu;
    m2 += all->weight[h] * (var + mu * mu);
  }
  *mean = m1;
  *variance = m2 - m1 * m1 > 0 ? m2 - m1 * m1 : 0;
}

/* Retention for each plan: pooled mean at each delay band, then each plan's average deviation from it, shrunk
 * towards none. Plans run at different delays are so compared fairly. */
typedef struct {
  double mean[SPEECHWARP_PLAN_COUNT];
  double sd[SPEECHWARP_PLAN_COUNT];
  int have[SPEECHWARP_PLAN_COUNT];
  int any;
} retention_summary;

static void summarise_retention(const speechwarp_trainer* t, retention_summary* out) {
  double band_sum[2] = {0, 0}, band_items[2] = {0, 0};
  double dev_sum[SPEECHWARP_PLAN_COUNT], dev_items[SPEECHWARP_PLAN_COUNT];
  double pooled_sum = 0, pooled_items = 0, pooled, spread;
  int k, p;

  memset(out, 0, sizeof(*out));
  memset(dev_sum, 0, sizeof(dev_sum));
  memset(dev_items, 0, sizeof(dev_items));
  for (k = 0; k < t->retention_count; k++) {
    const retention* r = &t->retentions[k];
    int band = r->delay > RETENTION_DAY;
    band_sum[band] += r->score * r->items;
    band_items[band] += r->items;
  }
  for (k = 0; k < t->retention_count; k++) {
    const retention* r = &t->retentions[k];
    int band = r->delay > RETENTION_DAY;
    int plan = t->sessions[r->session].plan;
    if (r->items <= 0) continue;
    dev_sum[plan] += (r->score - band_sum[band] / band_items[band]) * r->items;
    dev_items[plan] += r->items;
    pooled_sum += r->score * r->items;
    pooled_items += r->items;
  }
  if (pooled_items <= 0) {
    for (p = 0; p < SPEECHWARP_PLAN_COUNT; p++) {
      out->mean[p] = NAN;
      out->sd[p] = NAN;
    }
    return;
  }
  out->any = 1;
  pooled = pooled_sum / pooled_items;
  /* A binomial spread for one item at the pooled mean, kept away from 0. */
  spread = pooled * (1 - pooled);
  if (spread < 0.05) spread = 0.05;
  for (p = 0; p < SPEECHWARP_PLAN_COUNT; p++) {
    double items = dev_items[p] + RETENTION_PRIOR_ITEMS;
    out->mean[p] = pooled + dev_sum[p] / items;
    out->sd[p] = sqrt(spread / items);
    out->have[p] = dev_items[p] > 0;
  }
}

/* One draw of every plan's utility (H, then the noise, then the effects, then retention), and the best. */
static int draw_best(const models* all, const retention_summary* ret, double cost, rng* r) {
  const model* m;
  double s2, u = rng_uniform(r), kept[SPEECHWARP_PLAN_COUNT], best_kept = -HUGE_VAL, best_utility = -HUGE_VAL;
  int h = 0, p, best = 0;

  while (h < HALVINGS - 1 && u > all->weight[h]) {
    u -= all->weight[h];
    h++;
  }
  m = &all->fits[h];
  s2 = m->b / rng_gamma(r, m->a);
  for (p = 0; p < SPEECHWARP_PLAN_COUNT; p++) {
    kept[p] = ret->any ? ret->mean[p] + ret->sd[p] * rng_normal(r) : 0;
    if (kept[p] > best_kept) best_kept = kept[p];
  }
  /* The plan effects are independent given H and s^2 (each plan has its own column). */
  for (p = 0; p < SPEECHWARP_PLAN_COUNT; p++) {
    double effect = m->mean[p] + sqrt(s2 * m->cov[p][p]) * rng_normal(r);
    /* Practice slows every plan alike, so plans are compared on their effect before it does: then a point of
     * retention costs the same however far training has gone. */
    double utility = effect - cost * (best_kept - kept[p]);
    if (utility > best_utility) {
      best_utility = utility;
      best = p;
    }
  }
  return best;
}

int speechwarp_trainer_next_plan(speechwarp_trainer* trainer) {
  models all;
  retention_summary ret;
  if (!trainer) {
    return 0;
  }
  fit_all(trainer, &all);
  summarise_retention(trainer, &ret);
  return draw_best(&all, &ret, trainer->param[SPEECHWARP_PARAM_RETENTION_COST], &trainer->random);
}

static int valid_plan(int plan) { return plan >= 0 && plan < SPEECHWARP_PLAN_COUNT; }

double speechwarp_trainer_plan_effect(const speechwarp_trainer* trainer, int plan) {
  models all;
  double mean, variance;
  if (!trainer || !valid_plan(plan)) return NAN;
  fit_all(trainer, &all);
  gain_now(&all, plan, &mean, &variance);
  return mean;
}

double speechwarp_trainer_plan_effect_sd(const speechwarp_trainer* trainer, int plan) {
  models all;
  double mean, variance;
  if (!trainer || !valid_plan(plan)) return NAN;
  fit_all(trainer, &all);
  gain_now(&all, plan, &mean, &variance);
  return sqrt(variance);
}

double speechwarp_trainer_plan_retention(const speechwarp_trainer* trainer, int plan) {
  retention_summary ret;
  if (!trainer || !valid_plan(plan)) return NAN;
  summarise_retention(trainer, &ret);
  return ret.have[plan] ? ret.mean[plan] : NAN;
}

double speechwarp_trainer_plan_retention_sd(const speechwarp_trainer* trainer, int plan) {
  retention_summary ret;
  if (!trainer || !valid_plan(plan)) return NAN;
  summarise_retention(trainer, &ret);
  return ret.have[plan] ? ret.sd[plan] : NAN;
}

int speechwarp_trainer_plan_sessions(const speechwarp_trainer* trainer, int plan) {
  int count = 0, k;
  if (!trainer || !valid_plan(plan)) return 0;
  for (k = 0; k < trainer->session_count; k++) {
    if (trainer->sessions[k].valid && trainer->sessions[k].plan == plan) count++;
  }
  return count;
}

double speechwarp_trainer_plan_best_probability(const speechwarp_trainer* trainer, int plan) {
  models all;
  retention_summary ret;
  rng r;
  int wins = 0, d;
  if (!trainer || !valid_plan(plan)) return NAN;
  fit_all(trainer, &all);
  summarise_retention(trainer, &ret);
  /* Its own generator, so that asking does not change the trainer's next choice. */
  r.state = trainer->seed ^ 0xA5A5A5A5A5A5A5A5ull ^ (uint64_t)trainer->session_count;
  for (d = 0; d < BEST_DRAWS; d++) {
    if (draw_best(&all, &ret, trainer->param[SPEECHWARP_PARAM_RETENTION_COST], &r) == plan) wins++;
  }
  return (double)wins / BEST_DRAWS;
}

/* The halving time, averaged in log space over the grid (the last point stands for 1000 hours). */
static void halving_summary(const speechwarp_trainer* t, double* mean, double* sd) {
  models all;
  double m1 = 0, m2 = 0;
  int h;
  fit_all(t, &all);
  for (h = 0; h < HALVINGS; h++) {
    double x = log(h == HALVINGS - 1 ? 1000.0 : halving_hours[h]);
    m1 += all.weight[h] * x;
    m2 += all.weight[h] * x * x;
  }
  *mean = exp(m1);
  *sd = sqrt(m2 - m1 * m1 > 0 ? m2 - m1 * m1 : 0);
}

double speechwarp_trainer_trend(const speechwarp_trainer* trainer) {
  double mean, sd;
  if (!trainer) return NAN;
  halving_summary(trainer, &mean, &sd);
  return mean;
}

double speechwarp_trainer_trend_sd(const speechwarp_trainer* trainer) {
  double mean, sd;
  if (!trainer) return NAN;
  halving_summary(trainer, &mean, &sd);
  return sd;
}

/* ---- d. Blind trials ----------------------------------------------------------------------------------- */

#define MAX_VALUES 32
#define WINNER_HEARD 5
#define WINNER_MET 3

typedef struct {
  double values[MAX_VALUES];
  int value_count;
  int available;
} setting;

typedef struct {
  int setting;
  int band;
  int first, second; /* value numbers */
  double first_score, second_score;
  int preferred;
} trial;

struct speechwarp_trials {
  rng random;
  double confidence;
  setting* settings;
  int setting_count, setting_capacity;
  trial* trials;
  int trial_count, trial_capacity;
  double next_first, next_second;
};

static int band_of(double speed) { return (int)floor(speed); }

speechwarp_trials* speechwarp_trials_create(uint64_t seed) {
  speechwarp_trials* t = (speechwarp_trials*)calloc(1, sizeof(*t));
  if (!t) return NULL;
  t->random.state = seed;
  t->confidence = 0.95;
  t->next_first = NAN;
  t->next_second = NAN;
  return t;
}

void speechwarp_trials_destroy(speechwarp_trials* trials) {
  if (!trials) return;
  free(trials->settings);
  free(trials->trials);
  free(trials);
}

int speechwarp_trials_add_setting(speechwarp_trials* trials) {
  setting* s;
  if (!trials) return -1;
  if (trials->setting_count == trials->setting_capacity) {
    int capacity = trials->setting_capacity ? trials->setting_capacity * 2 : 8;
    setting* grown = (setting*)realloc(trials->settings, (size_t)capacity * sizeof(setting));
    if (!grown) return -1;
    trials->settings = grown;
    trials->setting_capacity = capacity;
  }
  s = &trials->settings[trials->setting_count];
  s->value_count = 0;
  s->available = 1;
  return trials->setting_count++;
}

static int valid_setting(const speechwarp_trials* trials, int setting_number) {
  return trials && setting_number >= 0 && setting_number < trials->setting_count;
}

static int find_value(const setting* s, double value) {
  int i;
  for (i = 0; i < s->value_count; i++) {
    if (s->values[i] == value) return i;
  }
  return -1;
}

int speechwarp_trials_add_value(speechwarp_trials* trials, int setting_number, double value) {
  setting* s;
  if (!valid_setting(trials, setting_number) || !is_finite(value)) return -1;
  s = &trials->settings[setting_number];
  if (s->value_count == MAX_VALUES || find_value(s, value) >= 0) return -1;
  s->values[s->value_count] = value;
  return s->value_count++;
}

void speechwarp_trials_set_available(speechwarp_trials* trials, int setting_number, int available) {
  if (valid_setting(trials, setting_number)) trials->settings[setting_number].available = available != 0;
}

int speechwarp_trials_add(speechwarp_trials* trials, int setting_number, double speed, double first_value,
                          double second_value, double first_score, double second_score, int preferred) {
  const setting* s;
  trial* t;
  int first, second;
  if (!valid_setting(trials, setting_number) || !(speed > 0) || !is_finite(speed) ||
      !(first_score >= 0 && first_score <= 1) || !(second_score >= 0 && second_score <= 1) || preferred < -1 ||
      preferred > 1) {
    return 0;
  }
  s = &trials->settings[setting_number];
  first = find_value(s, first_value);
  second = find_value(s, second_value);
  if (first < 0 || second < 0 || first == second) return 0;
  if (trials->trial_count == trials->trial_capacity) {
    int capacity = trials->trial_capacity ? trials->trial_capacity * 2 : 32;
    trial* grown = (trial*)realloc(trials->trials, (size_t)capacity * sizeof(trial));
    if (!grown) return 0;
    trials->trials = grown;
    trials->trial_capacity = capacity;
  }
  t = &trials->trials[trials->trial_count++];
  t->setting = setting_number;
  t->band = band_of(speed);
  t->first = first;
  t->second = second;
  t->first_score = first_score;
  t->second_score = second_score;
  t->preferred = preferred;
  return 1;
}

/* Trials of a pair of values (either order) of a setting in a band; -1 for b counts every pair with a. */
static int pair_count(const speechwarp_trials* trials, int setting_number, int band, int a, int b) {
  int count = 0, k;
  for (k = 0; k < trials->trial_count; k++) {
    const trial* t = &trials->trials[k];
    if (t->setting != setting_number || t->band != band) continue;
    if (b < 0 ? (t->first == a || t->second == a)
              : ((t->first == a && t->second == b) || (t->first == b && t->second == a))) {
      count++;
    }
  }
  return count;
}

int speechwarp_trials_next(speechwarp_trials* trials, double speed) {
  int band, chosen = -1, fewest = 0, ties = 0, k, i, j;
  const setting* s;
  int pair_a = -1, pair_b = -1, least = 0;
  if (!trials || !(speed > 0) || !is_finite(speed)) return -1;
  band = band_of(speed);
  /* The setting with the fewest trials here; ties chosen at random, uniformly (reservoir sampling). */
  for (i = 0; i < trials->setting_count; i++) {
    int count = 0;
    if (!trials->settings[i].available || trials->settings[i].value_count < 2) continue;
    for (k = 0; k < trials->trial_count; k++) {
      if (trials->trials[k].setting == i && trials->trials[k].band == band) count++;
    }
    if (chosen < 0 || count < fewest) {
      chosen = i;
      fewest = count;
      ties = 1;
    } else if (count == fewest && rng_below(&trials->random, ++ties) == 0) {
      chosen = i;
    }
  }
  if (chosen < 0) {
    trials->next_first = NAN;
    trials->next_second = NAN;
    return -1;
  }
  s = &trials->settings[chosen];
  ties = 0;
  for (i = 0; i < s->value_count; i++) {
    for (j = i + 1; j < s->value_count; j++) {
      int count = pair_count(trials, chosen, band, i, j);
      if (pair_a < 0 || count < least) {
        pair_a = i;
        pair_b = j;
        least = count;
        ties = 1;
      } else if (count == least && rng_below(&trials->random, ++ties) == 0) {
        pair_a = i;
        pair_b = j;
      }
    }
  }
  if (rng_below(&trials->random, 2) == 0) {
    trials->next_first = s->values[pair_a];
    trials->next_second = s->values[pair_b];
  } else {
    trials->next_first = s->values[pair_b];
    trials->next_second = s->values[pair_a];
  }
  return chosen;
}

double speechwarp_trials_next_first(const speechwarp_trials* trials) { return trials ? trials->next_first : NAN; }
double speechwarp_trials_next_second(const speechwarp_trials* trials) { return trials ? trials->next_second : NAN; }

typedef struct {
  int won, lost, tied, heard;
  double score_sum;
} tally;

/* Results for value `value` against `against` (-1: against any value). */
static tally count_value(const speechwarp_trials* trials, int setting_number, int band, int value, int against) {
  tally r;
  int k;
  memset(&r, 0, sizeof(r));
  for (k = 0; k < trials->trial_count; k++) {
    const trial* t = &trials->trials[k];
    int first;
    if (t->setting != setting_number || t->band != band) continue;
    if (t->first == value && (against < 0 || t->second == against)) {
      first = 1;
    } else if (t->second == value && (against < 0 || t->first == against)) {
      first = 0;
    } else {
      continue;
    }
    r.heard++;
    r.score_sum += first ? t->first_score : t->second_score;
    if (t->preferred == 0) {
      r.tied++;
    } else if ((t->preferred == -1) == first) {
      r.won++;
    } else {
      r.lost++;
    }
  }
  return r;
}

static int valid_value(const speechwarp_trials* trials, int setting_number, int value) {
  return valid_setting(trials, setting_number) && value >= 0 && value < trials->settings[setting_number].value_count;
}

int speechwarp_trials_won(const speechwarp_trials* trials, int setting_number, double speed, int value) {
  return valid_value(trials, setting_number, value) ? count_value(trials, setting_number, band_of(speed), value, -1).won : 0;
}

int speechwarp_trials_lost(const speechwarp_trials* trials, int setting_number, double speed, int value) {
  return valid_value(trials, setting_number, value) ? count_value(trials, setting_number, band_of(speed), value, -1).lost : 0;
}

int speechwarp_trials_tied(const speechwarp_trials* trials, int setting_number, double speed, int value) {
  return valid_value(trials, setting_number, value) ? count_value(trials, setting_number, band_of(speed), value, -1).tied : 0;
}

int speechwarp_trials_heard(const speechwarp_trials* trials, int setting_number, double speed, int value) {
  return valid_value(trials, setting_number, value) ? count_value(trials, setting_number, band_of(speed), value, -1).heard : 0;
}

double speechwarp_trials_mean_score(const speechwarp_trials* trials, int setting_number, double speed, int value) {
  tally r;
  if (!valid_value(trials, setting_number, value)) return NAN;
  r = count_value(trials, setting_number, band_of(speed), value, -1);
  return r.heard > 0 ? r.score_sum / r.heard : NAN;
}

/* P(p > 1/2) for p ~ Beta(a, b), a and b at least 1, by Simpson's rule on the density. */
static double beta_above_half(double a, double b) {
  const int steps = 2000;
  double norm = lgamma(a + b) - lgamma(a) - lgamma(b), sum = 0, h = 0.5 / steps;
  int i;
  for (i = 0; i <= steps; i++) {
    double x = 0.5 + i * h, f;
    if (x >= 1) {
      f = b == 1 ? exp(norm) : 0; /* the density at 1 */
    } else {
      f = exp(norm + (a - 1) * log(x) + (b - 1) * log(1 - x));
    }
    sum += f * (i == 0 || i == steps ? 1 : i % 2 ? 4 : 2);
  }
  return sum * h / 3;
}

/* The Bayes factor for "v is preferred" against "no preference": a preference p uniform on (1/2, 1) against
 * p = 1/2, after `won` and `lost` (ties count half each way). Under no preference it is a supermartingale, so
 * by Ville's inequality the chance that it ever reaches k, however often it is looked at, is at most 1/k. */
static double bayes_factor(double won, double lost) {
  double n = won + lost;
  double log_beta = lgamma(won + 1) + lgamma(lost + 1) - lgamma(n + 2);
  return exp((n + 1) * log(2.0) + log_beta) * beta_above_half(won + 1, lost + 1);
}

int speechwarp_trials_winner(const speechwarp_trials* trials, int setting_number, double speed) {
  const setting* s;
  int band, v, w;
  if (!valid_setting(trials, setting_number) || !(speed > 0) || !is_finite(speed)) return -1;
  s = &trials->settings[setting_number];
  band = band_of(speed);
  for (v = 0; v < s->value_count; v++) {
    int wins_all = 1;
    if (count_value(trials, setting_number, band, v, -1).heard < WINNER_HEARD) continue;
    for (w = 0; w < s->value_count && wins_all; w++) {
      tally r;
      if (w == v) continue;
      r = count_value(trials, setting_number, band, v, w);
      if (r.heard < WINNER_MET ||
          bayes_factor(r.won + 0.5 * r.tied, r.lost + 0.5 * r.tied) < 1 / (1 - trials->confidence)) {
        wins_all = 0;
      }
    }
    if (wins_all && s->value_count > 1) return v;
  }
  return -1;
}

void speechwarp_trials_set_confidence(speechwarp_trials* trials, double confidence) {
  if (trials && confidence >= 0.5 && confidence < 1) trials->confidence = confidence;
}
