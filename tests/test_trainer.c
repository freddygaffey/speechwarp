/* Tests of the listener trainer and the blind trials, against simulated listeners whose threshold, learning
 * rate and forgetting rate are known. */
#include <string.h>

#include "speechwarp.h"
#include "test_util.h"

#define HOUR 3600.0
#define DAY (24 * HOUR)

/* A listener who understands u(r) = 1 / (1 + exp(slope (ln r - ln threshold) - ln 3)) of a sentence at r
 * syllables a second: 75% at the threshold. */
typedef struct {
  double threshold;
  double slope;
} listener;

static double understood(const listener* l, double rate) {
  return 1 / (1 + exp(l->slope * (log(rate) - log(l->threshold)) - log(3.0)));
}

/* A sentence of eight words repeated back: each word right with chance u, less a 3% slip. */
static double repeat_back(const listener* l, double rate) {
  double u = understood(l, rate) * 0.97;
  int right = 0, i;
  for (i = 0; i < 8; i++) {
    if (random_unit() < u) right++;
  }
  return right / 8.0;
}

/* A verification item: right if understood, else a coin toss. */
static double verify(const listener* l, double rate) {
  return random_unit() < understood(l, rate) * 0.97 ? 1 : (random_unit() < 0.5 ? 1 : 0);
}

/* Runs a threshold test to the end; returns the presentations it took. */
static int run_test(speechwarp_trainer* t, const listener* l, double prior, double* time) {
  int n = 0;
  speechwarp_trainer_test_begin(t, prior, *time);
  while (!speechwarp_trainer_test_done(t)) {
    double rate = speechwarp_trainer_test_rate(t);
    if (n % 3 == 2) {
      speechwarp_trainer_add_measure(t, SPEECHWARP_MEASURE_VERIFICATION, verify(l, rate), 1, rate, *time);
    } else {
      speechwarp_trainer_add_measure(t, SPEECHWARP_MEASURE_INTELLIGIBILITY, repeat_back(l, rate), 8, rate, *time);
    }
    *time += 6;
    n++;
  }
  speechwarp_trainer_test_end(t, *time);
  return n;
}

static int compare_doubles(const void* a, const void* b) {
  double x = *(const double*)a, y = *(const double*)b;
  return x < y ? -1 : x > y;
}

static void test_threshold_recovery(void) {
  static const double thresholds[] = {7, 13, 21, 30};
  const int runs = 40;
  int k;
  for (k = 0; k < 4; k++) {
    double errors[40], ratios[40];
    int covered = 0, presentations = 0, r;
    for (r = 0; r < runs; r++) {
      speechwarp_trainer* t = speechwarp_trainer_create((uint64_t)(r + 1));
      listener l;
      double time = 0, estimate;
      l.threshold = thresholds[k];
      l.slope = 4 + 8 * random_unit();
      random_state = (uint32_t)(1000 * k + r + 7);
      presentations += run_test(t, &l, 0, &time);
      estimate = speechwarp_trainer_threshold(t);
      ratios[r] = speechwarp_trainer_threshold_high(t) / speechwarp_trainer_threshold_low(t);
      errors[r] = fabs(log(estimate / l.threshold));
      if (speechwarp_trainer_threshold_low(t) <= l.threshold && l.threshold <= speechwarp_trainer_threshold_high(t)) {
        covered++;
      }
      CHECK(speechwarp_trainer_threshold_low(t) < estimate && estimate < speechwarp_trainer_threshold_high(t));
      speechwarp_trainer_destroy(t);
    }
    qsort(errors, runs, sizeof(double), compare_doubles);
    qsort(ratios, runs, sizeof(double), compare_doubles);
    printf("interval high/low: median %.3f\n", ratios[runs / 2]);
    printf("threshold %4.0f: median error %4.1f%%, 90th %4.1f%%, 95%% interval covers %d/%d, %.1f presentations\n",
           thresholds[k], 100 * errors[runs / 2], 100 * errors[runs * 9 / 10], covered, runs,
           (double)presentations / runs);
    CHECK(errors[runs / 2] < 0.08);      /* typically within 8% */
    CHECK(errors[runs * 9 / 10] < 0.2);
    CHECK(covered >= runs * 85 / 100);   /* the 95% interval is honest */
    CHECK(presentations <= runs * 40);
  }
}

static void test_session_plans(void) {
  speechwarp_trainer* t = speechwarp_trainer_create(1);
  listener l = {12, 8};
  double time = 0, base, rate, previous;
  int i;

  random_state = 3;
  run_test(t, &l, 0, &time);
  base = speechwarp_trainer_threshold(t);
  CHECK(base > 0);

  CHECK(speechwarp_trainer_session_rate(t, time) == 0); /* no session */

  speechwarp_trainer_session_begin(t, SPEECHWARP_PLAN_STEADY, time);
  CHECK(fabs(speechwarp_trainer_session_rate(t, time + 1000) - base * 1.1) < 1e-9);
  speechwarp_trainer_session_end(t, 0.5, time + 1800);

  /* Ramp: 80% of threshold + 10%, 2% of it every 2 minutes, then holds. */
  speechwarp_trainer_session_begin(t, SPEECHWARP_PLAN_RAMP, time);
  CHECK(fabs(speechwarp_trainer_session_rate(t, time) - base * 1.1 * 0.8) < 1e-9);
  CHECK(fabs(speechwarp_trainer_session_rate(t, time + 121) - base * 1.1 * 0.82) < 1e-9);
  previous = 0;
  for (i = 0; i < 60; i++) {
    rate = speechwarp_trainer_session_rate(t, time + i * 60);
    CHECK(rate >= previous);
    CHECK(rate <= base * 1.1 + 1e-9);
    previous = rate;
  }
  CHECK(fabs(previous - base * 1.1) < 1e-9);
  speechwarp_trainer_session_end(t, 1, time + 3600);

  /* Interval: above for 10 minutes, then below. */
  speechwarp_trainer_session_begin(t, SPEECHWARP_PLAN_INTERVAL, time);
  CHECK(fabs(speechwarp_trainer_session_rate(t, time + 300) - base * 1.15) < 1e-9);
  CHECK(fabs(speechwarp_trainer_session_rate(t, time + 900) - base * 0.85) < 1e-9);
  CHECK(fabs(speechwarp_trainer_session_rate(t, time + 1500) - base * 1.15) < 1e-9);
  speechwarp_trainer_session_end(t, 0.5, time + 1800);

  /* Tracking: checks move the rate towards where 75% is understood. */
  speechwarp_trainer_session_begin(t, SPEECHWARP_PLAN_TRACKING, time);
  rate = speechwarp_trainer_session_rate(t, time);
  CHECK(fabs(rate - base) < 1e-9);
  speechwarp_trainer_add_measure(t, SPEECHWARP_MEASURE_INTELLIGIBILITY, 1, 1, rate, time + 600);
  CHECK(speechwarp_trainer_session_rate(t, time + 601) > rate);
  rate = speechwarp_trainer_session_rate(t, time + 601);
  speechwarp_trainer_add_measure(t, SPEECHWARP_MEASURE_INTELLIGIBILITY, 0.2, 1, rate, time + 1200);
  CHECK(speechwarp_trainer_session_rate(t, time + 1201) < rate);
  /* A long run of checks settles near the listener's threshold. */
  for (i = 0; i < 400; i++) {
    rate = speechwarp_trainer_session_rate(t, time + 1300 + i);
    speechwarp_trainer_add_measure(t, SPEECHWARP_MEASURE_INTELLIGIBILITY, repeat_back(&l, rate), 1, rate,
                                   time + 1300 + i);
  }
  {
    double sum = 0;
    for (i = 0; i < 200; i++) {
      rate = speechwarp_trainer_session_rate(t, time + 2000 + i);
      sum += log(rate);
      speechwarp_trainer_add_measure(t, SPEECHWARP_MEASURE_INTELLIGIBILITY, repeat_back(&l, rate), 1, rate,
                                     time + 2000 + i);
    }
    printf("tracking settles at %.1f for a threshold of %.1f\n", exp(sum / 200), l.threshold);
    CHECK(fabs(sum / 200 - log(l.threshold)) < 0.12);
  }
  /* No test ended during it, so it is not used for comparing plans. */
  CHECK(speechwarp_trainer_session_end(t, 1, time + 4000) == 3);
  CHECK(speechwarp_trainer_plan_sessions(t, SPEECHWARP_PLAN_TRACKING) == 0);
  CHECK(speechwarp_trainer_session_end(t, 1, time + 4000) == -1);
  speechwarp_trainer_destroy(t);
}

/* Four plans with known effects. Interval trains fastest but costs retention; tracking is the best once
 * retention counts. Gains shrink with practice, and the listener forgets between sessions. */
typedef struct {
  double gain;      /* fraction of the threshold a hour, at the start */
  double retention; /* chance of a retention item right */
} true_plan;

static const true_plan plans[SPEECHWARP_PLAN_COUNT] = {
    {0.000, 0.85}, /* steady */
    {0.015, 0.85}, /* ramp */
    {0.060, 0.50}, /* interval: fastest, but forgets the content */
    {0.040, 0.85}, /* tracking: the best */
};

static int simulate_plans(uint64_t seed, int sessions, int* chosen_late, speechwarp_trainer** out) {
  speechwarp_trainer* t = speechwarp_trainer_create(seed);
  listener l = {6, 8};
  double time = 0, practice = 0;
  int s, i;

  random_state = (uint32_t)seed * 77u + 5u;
  memset(chosen_late, 0, sizeof(int) * SPEECHWARP_PLAN_COUNT);
  run_test(t, &l, 0, &time);
  for (s = 0; s < sessions; s++) {
    int plan = speechwarp_trainer_next_plan(t), number;
    double hours = 2, learning = 1 / (1 + practice / 20); /* gains halve after 20 hours */
    if (s >= sessions * 2 / 3) chosen_late[plan]++;
    if (s > 0) run_test(t, &l, 0, &time);
    speechwarp_trainer_session_begin(t, plan, time);
    for (i = 0; i < 12; i++) { /* a check every 10 minutes */
      double rate = speechwarp_trainer_session_rate(t, time + i * 600);
      speechwarp_trainer_add_measure(t, SPEECHWARP_MEASURE_INTELLIGIBILITY, repeat_back(&l, rate), 1, rate,
                                     time + i * 600);
    }
    time += hours * HOUR;
    l.threshold *= exp(plans[plan].gain * learning * hours);
    practice += hours;
    run_test(t, &l, 0, &time);
    number = speechwarp_trainer_session_end(t, hours, time);
    for (i = 0; i < 2; i++) { /* a day and a week later, ten items each */
      int right = 0, k;
      for (k = 0; k < 10; k++) right += random_unit() < plans[plan].retention;
      speechwarp_trainer_add_retention(t, number, right / 10.0, 10, i ? 7 * DAY : DAY, time + (i ? 7 : 1) * DAY);
    }
    time += DAY;
    l.threshold *= 0.99; /* forgets a little by the next day */
  }
  *out = t;
  return 0;
}

static void test_plan_comparison(void) {
  int chosen_late[SPEECHWARP_PLAN_COUNT], total[SPEECHWARP_PLAN_COUNT] = {0, 0, 0, 0};
  int seeds = 6, k, p, tracking_best = 0;
  for (k = 0; k < seeds; k++) {
    speechwarp_trainer* t;
    simulate_plans((uint64_t)(k + 11), 60, chosen_late, &t);
    printf("seed %d, last third chose:", k + 11);
    for (p = 0; p < SPEECHWARP_PLAN_COUNT; p++) {
      total[p] += chosen_late[p];
      printf(" %d", chosen_late[p]);
    }
    printf("; gain/h now:");
    for (p = 0; p < SPEECHWARP_PLAN_COUNT; p++) {
      printf(" %.3f+-%.3f", speechwarp_trainer_plan_effect(t, p), speechwarp_trainer_plan_effect_sd(t, p));
    }
    printf("; retention:");
    for (p = 0; p < SPEECHWARP_PLAN_COUNT; p++) printf(" %.2f", speechwarp_trainer_plan_retention(t, p));
    printf("; P(best):");
    for (p = 0; p < SPEECHWARP_PLAN_COUNT; p++) printf(" %.2f", speechwarp_trainer_plan_best_probability(t, p));
    printf("; halving %.0f h (sd %.2f in ln); threshold %.1f\n", speechwarp_trainer_trend(t), speechwarp_trainer_trend_sd(t), speechwarp_trainer_threshold(t));
    {
      int highest = 1;
      for (p = 0; p < SPEECHWARP_PLAN_COUNT; p++) {
        if (p != SPEECHWARP_PLAN_TRACKING && speechwarp_trainer_plan_best_probability(t, p) >=
                                                 speechwarp_trainer_plan_best_probability(t, SPEECHWARP_PLAN_TRACKING)) {
          highest = 0;
        }
      }
      tracking_best += highest;
    }
    /* Interval's raw gain is measured as the highest, and its retention as the lowest. */
    CHECK(speechwarp_trainer_plan_effect(t, SPEECHWARP_PLAN_INTERVAL) >
          speechwarp_trainer_plan_effect(t, SPEECHWARP_PLAN_STEADY));
    CHECK(speechwarp_trainer_plan_retention(t, SPEECHWARP_PLAN_INTERVAL) <
          speechwarp_trainer_plan_retention(t, SPEECHWARP_PLAN_TRACKING));
    CHECK(speechwarp_trainer_trend(t) < 200); /* gains shrink with practice (truly halve by 20 hours) */
    speechwarp_trainer_destroy(t);
  }
  CHECK(tracking_best >= seeds - 1); /* the most likely best plan, after 60 sessions */
  /* Over the last third, tracking was chosen more than any other plan. */
  for (p = 0; p < SPEECHWARP_PLAN_COUNT; p++) {
    if (p != SPEECHWARP_PLAN_TRACKING) CHECK(total[SPEECHWARP_PLAN_TRACKING] > total[p]);
  }
}

static void test_determinism(void) {
  speechwarp_trainer* a = speechwarp_trainer_create(42);
  speechwarp_trainer* b = speechwarp_trainer_create(42);
  int i;
  for (i = 0; i < 20; i++) {
    CHECK(speechwarp_trainer_next_plan(a) == speechwarp_trainer_next_plan(b));
  }
  speechwarp_trainer_test_begin(a, 0, 0);
  speechwarp_trainer_test_begin(b, 0, 0);
  for (i = 0; i < 10; i++) {
    double ra = speechwarp_trainer_test_rate(a), rb = speechwarp_trainer_test_rate(b);
    CHECK(ra == rb);
    speechwarp_trainer_add_measure(a, SPEECHWARP_MEASURE_INTELLIGIBILITY, i % 2 ? 0.9 : 0.4, 1, ra, i);
    speechwarp_trainer_add_measure(b, SPEECHWARP_MEASURE_INTELLIGIBILITY, i % 2 ? 0.9 : 0.4, 1, rb, i);
  }
  CHECK(speechwarp_trainer_threshold(a) == speechwarp_trainer_threshold(b));
  CHECK(speechwarp_trainer_plan_best_probability(a, 0) == speechwarp_trainer_plan_best_probability(b, 0));
  speechwarp_trainer_destroy(a);
  speechwarp_trainer_destroy(b);
}

static void test_trainer_arguments(void) {
  speechwarp_trainer* t = speechwarp_trainer_create(0);
  double nan = sqrt(-1.0);
  CHECK(t != NULL);
  CHECK(speechwarp_trainer_threshold(t) == 0);
  CHECK(speechwarp_trainer_test_rate(t) == 0);
  CHECK(!speechwarp_trainer_test_done(t));
  CHECK(speechwarp_trainer_test_end(t, 0) == 0);
  CHECK(!speechwarp_trainer_add_measure(t, SPEECHWARP_MEASURE_RETENTION, 0.5, 1, 10, 0));
  CHECK(!speechwarp_trainer_add_measure(t, 7, 0.5, 1, 10, 0));
  CHECK(!speechwarp_trainer_add_measure(t, SPEECHWARP_MEASURE_INTELLIGIBILITY, 1.5, 1, 10, 0));
  CHECK(!speechwarp_trainer_add_measure(t, SPEECHWARP_MEASURE_INTELLIGIBILITY, nan, 1, 10, 0));
  CHECK(!speechwarp_trainer_add_measure(t, SPEECHWARP_MEASURE_INTELLIGIBILITY, 0.5, 0, 10, 0));
  CHECK(!speechwarp_trainer_add_measure(t, SPEECHWARP_MEASURE_INTELLIGIBILITY, 0.5, 1, -1, 0));
  CHECK(speechwarp_trainer_add_measure(t, SPEECHWARP_MEASURE_RATING, 0.5, 1, 10, 0));
  CHECK(!speechwarp_trainer_add_retention(t, 0, 0.5, 1, DAY, 0)); /* no such session */
  CHECK(speechwarp_trainer_get_weight(t, SPEECHWARP_MEASURE_RATING) == 0.3);
  speechwarp_trainer_set_weight(t, SPEECHWARP_MEASURE_RATING, -1);
  speechwarp_trainer_set_weight(t, SPEECHWARP_MEASURE_RATING, nan);
  CHECK(speechwarp_trainer_get_weight(t, SPEECHWARP_MEASURE_RATING) == 0.3);
  speechwarp_trainer_set_weight(t, SPEECHWARP_MEASURE_RATING, 0);
  CHECK(speechwarp_trainer_get_weight(t, SPEECHWARP_MEASURE_RATING) == 0);
  CHECK(speechwarp_trainer_get_param(t, SPEECHWARP_PARAM_TARGET) == 0.75);
  speechwarp_trainer_set_param(t, SPEECHWARP_PARAM_TARGET, 2);
  CHECK(speechwarp_trainer_get_param(t, SPEECHWARP_PARAM_TARGET) == 0.95);
  speechwarp_trainer_set_param(t, SPEECHWARP_PARAM_TARGET, nan);
  CHECK(speechwarp_trainer_get_param(t, SPEECHWARP_PARAM_TARGET) == 0.95);
  CHECK(speechwarp_trainer_plan_sessions(t, 0) == 0);
  CHECK(speechwarp_trainer_plan_retention(t, 0) != speechwarp_trainer_plan_retention(t, 0)); /* NaN */
  CHECK(speechwarp_trainer_plan_effect(t, 0) == 0); /* the prior */
  CHECK(speechwarp_trainer_plan_effect(t, 9) != speechwarp_trainer_plan_effect(t, 9));
  {
    int counts[SPEECHWARP_PLAN_COUNT] = {0, 0, 0, 0}, i, p;
    for (i = 0; i < 400; i++) counts[speechwarp_trainer_next_plan(t)]++;
    for (p = 0; p < SPEECHWARP_PLAN_COUNT; p++) CHECK(counts[p] > 50); /* with no data, every plan is tried */
  }
  speechwarp_trainer_destroy(t);
  speechwarp_trainer_destroy(NULL);
}

/* ---- Blind trials ---- */

static void test_trials_design(void) {
  speechwarp_trials* t = speechwarp_trials_create(5);
  int pause = speechwarp_trials_add_setting(t), floor_setting = speechwarp_trials_add_setting(t);
  int method = speechwarp_trials_add_setting(t);
  int counts[3] = {0, 0, 0}, pairs[3] = {0, 0, 0}, first_low = 0, i;

  CHECK(pause == 0 && floor_setting == 1 && method == 2);
  CHECK(speechwarp_trials_next(t, 5.5) == -1); /* no setting has two values */
  CHECK(speechwarp_trials_add_value(t, pause, 0) == 0);
  CHECK(speechwarp_trials_add_value(t, pause, 0.015) == 1);
  CHECK(speechwarp_trials_add_value(t, pause, 0.03) == 2);
  CHECK(speechwarp_trials_add_value(t, pause, 0.03) == -1);
  CHECK(speechwarp_trials_add_value(t, 9, 1) == -1);
  speechwarp_trials_add_value(t, floor_setting, 0);
  speechwarp_trials_add_value(t, floor_setting, 0.5);
  speechwarp_trials_add_value(t, method, 0);
  speechwarp_trials_add_value(t, method, 1);
  speechwarp_trials_set_available(t, method, 0);

  /* Settings are taken in turn, and pairs of a setting in turn, in either order. */
  for (i = 0; i < 60; i++) {
    int s = speechwarp_trials_next(t, 5.5);
    double a = speechwarp_trials_next_first(t), b = speechwarp_trials_next_second(t);
    CHECK(s == pause || s == floor_setting);
    counts[s]++;
    if (s == pause) {
      double low = a < b ? a : b, high = a < b ? b : a;
      pairs[low == 0 ? (high == 0.015 ? 0 : 1) : 2]++;
    }
    if (a < b) first_low++;
    CHECK(speechwarp_trials_add(t, s, 5.5, a, b, 0.5, 0.5, 0));
  }
  CHECK(counts[0] == 30 && counts[1] == 30 && counts[2] == 0);
  CHECK(pairs[0] == 10 && pairs[1] == 10 && pairs[2] == 10);
  CHECK(first_low > 15 && first_low < 45);
  /* Another band starts afresh, and a band is a whole number of speed. */
  CHECK(speechwarp_trials_heard(t, pause, 6.0, 0) == 0);
  CHECK(speechwarp_trials_heard(t, pause, 5.01, 0) == 20);
  CHECK(speechwarp_trials_tied(t, pause, 5.9, 0) == 20);
  CHECK(fabs(speechwarp_trials_mean_score(t, pause, 5.5, 0) - 0.5) < 1e-12);
  CHECK(speechwarp_trials_mean_score(t, pause, 7, 0) != speechwarp_trials_mean_score(t, pause, 7, 0));
  CHECK(speechwarp_trials_winner(t, pause, 5.5) == -1); /* all ties */
  CHECK(!speechwarp_trials_add(t, pause, 5.5, 0, 0.5, 0.5, 0.5, 0)); /* not a value */
  CHECK(!speechwarp_trials_add(t, pause, 5.5, 0, 0, 0.5, 0.5, 0));
  CHECK(!speechwarp_trials_add(t, pause, 5.5, 0, 0.015, 0.5, 0.5, 2));
  speechwarp_trials_destroy(t);
}

/* A listener who prefers a pause of 15 ms heard 85% of the time to either other value. */
static void test_trials_winner(void) {
  speechwarp_trials* t = speechwarp_trials_create(9);
  int s = speechwarp_trials_add_setting(t), i, found_at = -1;
  speechwarp_trials_add_value(t, s, 0);
  speechwarp_trials_add_value(t, s, 0.015);
  speechwarp_trials_add_value(t, s, 0.03);
  random_state = 99;
  for (i = 0; i < 120 && found_at < 0; i++) {
    double a, b;
    int preferred;
    speechwarp_trials_next(t, 6.5);
    a = speechwarp_trials_next_first(t);
    b = speechwarp_trials_next_second(t);
    if (a == 0.015 || b == 0.015) {
      int good_first = a == 0.015;
      int picks_good = random_unit() < 0.85;
      preferred = picks_good == good_first ? -1 : 1;
    } else {
      preferred = random_unit() < 0.5 ? -1 : 1;
    }
    speechwarp_trials_add(t, s, 6.5, a, b, 0.6, 0.6, preferred);
    if (speechwarp_trials_winner(t, s, 6.5) >= 0) found_at = i + 1;
  }
  printf("a value preferred 85%% of the time wins after %d trials\n", found_at);
  CHECK(found_at > 0 && found_at < 60);
  CHECK(speechwarp_trials_winner(t, s, 6.5) == 1);
  CHECK(speechwarp_trials_winner(t, s, 4.5) == -1);
  speechwarp_trials_destroy(t);

  /* No preference: no winner in 90 trials, for most seeds. */
  {
    int false_winners = 0, seed;
    for (seed = 0; seed < 20; seed++) {
      speechwarp_trials* u = speechwarp_trials_create((uint64_t)seed);
      int v = speechwarp_trials_add_setting(u), any = 0;
      speechwarp_trials_add_value(u, v, 0);
      speechwarp_trials_add_value(u, v, 1);
      random_state = (uint32_t)seed + 1000;
      for (i = 0; i < 90; i++) {
        speechwarp_trials_next(u, 5);
        speechwarp_trials_add(u, v, 5, speechwarp_trials_next_first(u), speechwarp_trials_next_second(u), 0.5, 0.5,
                              random_unit() < 0.5 ? -1 : 1);
        if (speechwarp_trials_winner(u, v, 5) >= 0) any = 1;
      }
      false_winners += any;
      speechwarp_trials_destroy(u);
    }
    printf("no preference: a winner appeared at some point in %d of 20 runs\n", false_winners);
    CHECK(false_winners <= 3); /* at most 5% each way, however often it is checked */
  }
}

/* Scores a pair and checks the share and the four counts. */
static void check_score(const char* reference, const char* heard, double share, int right, int missed, int wrong,
                        int extra) {
  int counts[4] = {-9, -9, -9, -9};
  double got = speechwarp_score_words(reference, heard, counts);
  if (fabs(got - share) > 1e-12 || counts[0] != right || counts[1] != missed || counts[2] != wrong ||
      counts[3] != extra) {
    printf("score_words(\"%s\", \"%s\") = %g [%d %d %d %d], wanted %g [%d %d %d %d]\n", reference, heard, got,
           counts[0], counts[1], counts[2], counts[3], share, right, missed, wrong, extra);
  }
  CHECK(fabs(got - share) <= 1e-12);
  CHECK(counts[0] == right && counts[1] == missed && counts[2] == wrong && counts[3] == extra);
}

static void test_score_words(void) {
  /* Exact, punctuation, case. */
  check_score("The cat sat on the mat", "the cat sat on the mat", 1, 6, 0, 0, 0);
  check_score("The cat sat on the mat.", "\"The cat, sat on - the MAT!\"", 1, 6, 0, 0, 0);
  check_score("HELLO there", "hello THERE", 1, 2, 0, 0, 0);
  /* Contractions, either apostrophe; apostrophes at word edges separate. */
  check_score("Don't stop", "don\xe2\x80\x99t stop", 1, 2, 0, 0, 0);
  check_score("don't", "dont", 0, 0, 0, 1, 0);
  check_score("the dogs' toys", "'the dogs toys'", 1, 3, 0, 0, 0);
  check_score("I can't", "I can t", 0.5, 1, 0, 1, 1);
  /* Insertions, deletions, substitutions. */
  check_score("the cat sat", "the big cat sat down", 1, 3, 0, 0, 2);
  check_score("the cat sat on the mat", "the cat on mat", 4.0 / 6, 4, 2, 0, 0);
  check_score("the cat sat", "the dog sat", 2.0 / 3, 2, 0, 1, 0);
  check_score("one two three four", "one too tree four five", 0.5, 2, 0, 2, 1);
  /* Ties go to right: a swapped pair is one right, one missed, one extra, not two wrong. */
  check_score("a b", "b a", 0.5, 1, 1, 0, 1);
  /* Empty. */
  check_score("", "", 1, 0, 0, 0, 0);
  check_score("  ...  ", "", 1, 0, 0, 0, 0);
  check_score("", "something", 0, 0, 0, 0, 1);
  check_score("hello world", "", 0, 0, 2, 0, 0);
  check_score("hello world", "?!", 0, 0, 2, 0, 0);
  CHECK(speechwarp_score_words(NULL, NULL, NULL) == 1);
  CHECK(speechwarp_score_words("a b", NULL, NULL) == 0);
  /* Numbers stay digits; commas join, full stops stay, between digits. */
  check_score("I have 3 cats", "I have three cats", 0.75, 3, 0, 1, 0);
  check_score("it cost 1,000 pounds", "It cost 1000 pounds.", 1, 4, 0, 0, 0);
  check_score("pi is 3.14", "pi is 3.14.", 1, 3, 0, 0, 0);
  check_score("pi is 3.14", "pi is 3 14", 2.0 / 3, 2, 0, 1, 1);
  /* UTF-8: accented letters are word content and fold to lower case; Unicode quotes and dashes separate. */
  check_score("caf\xc3\xa9 au lait", "CAF\xc3\x89 au lait", 1, 3, 0, 0, 0);
  check_score("caf\xc3\xa9", "cafe", 0, 0, 0, 1, 0);
  check_score("\xc5\x81\xc3\xb3" "d\xc5\xba", "\xc5\x82\xc3\x93" "D\xc5\xb9", 1, 1, 0, 0, 0); /* Łódź */
  check_score("STRA\xe1\xba\x9e" "E", "stra\xc3\x9f" "e", 1, 1, 0, 0, 0);                 /* capital sharp s */
  check_score("\xe2\x80\x9cWell\xe2\x80\x94yes\xe2\x80\xa6\xe2\x80\x9d", "well yes", 1, 2, 0, 0, 0);
  check_score("\xd0\xbc\xd0\xb8\xd1\x80 \xe4\xb8\x96\xe7\x95\x8c", "\xd0\xbc\xd0\xb8\xd1\x80 \xe4\xb8\x96\xe7\x95\x8c", 1,
              2, 0, 0, 0);
  /* Malformed UTF-8 does not crash and compares byte for byte. */
  check_score("a\xff" "b c", "a\xff" "b c", 1, 2, 0, 0, 0);
  check_score("\xe2\x80", "\xe2\x80", 1, 1, 0, 0, 0);
  /* A longer passage: counts add up. */
  {
    int counts[4];
    const char* reference = "It was the best of times, it was the worst of times, it was the age of wisdom.";
    const char* heard = "it was the best of time it was worst of times it was an age of wisdom yes";
    double share = speechwarp_score_words(reference, heard, counts);
    CHECK(counts[0] + counts[1] + counts[2] == 18);
    CHECK(counts[0] + counts[2] + counts[3] == 18);
    CHECK(fabs(share - counts[0] / 18.0) < 1e-12);
    CHECK(counts[0] == 15 && counts[1] == 1 && counts[2] == 2 && counts[3] == 1);
  }
}

int main(void) {
  test_trainer_arguments();
  test_determinism();
  test_session_plans();
  test_threshold_recovery();
  test_plan_comparison();
  test_trials_design();
  test_trials_winner();
  test_score_words();
  if (failures) {
    printf("%d check(s) failed\n", failures);
    return 1;
  }
  printf("trainer tests passed\n");
  return 0;
}
