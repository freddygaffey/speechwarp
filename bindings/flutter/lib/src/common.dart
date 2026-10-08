// Pieces shared by the native (ffi.dart) and web (web.dart) implementations, so that they are written once.

/// What a score in 0..1 measures.
enum TrainerMeasure {
  /// Share of the words said back correctly from a sentence heard once.
  intelligibility(0),

  /// Share right on "was this sentence in what you just heard?" items. Chance is 0.5.
  verification(1),

  /// Verification items about a session's material, answered after a delay; see [ListenerTrainer.addRetention].
  retention(2),

  /// The listener's own "how well did you follow?", 1 to 5 scaled to 0..1 as (r - 1) / 4.
  rating(3);

  const TrainerMeasure(this.value);

  /// The number the C library uses.
  final int value;
}

/// Session plans: how the rate moves during a session.
enum TrainerPlan {
  /// The threshold plus a margin, all session.
  steady(0),

  /// Start below the threshold and step up to threshold plus margin.
  ramp(1),

  /// Alternate periods above and below the threshold.
  interval(2),

  /// Move up or down after each in-session check, to stay at the target.
  tracking(3);

  const TrainerPlan(this.value);

  /// The number the C library uses.
  final int value;
}

/// Tunable numbers of the trainer, with their defaults.
enum TrainerParam {
  /// Share understood that defines the threshold: 0.75 (0.5 to 0.95).
  target(0),

  /// Steady and ramp: aim this fraction above the threshold: 0.10.
  margin(1),

  /// Ramp: start at this fraction of the target rate: 0.8.
  rampStart(2),

  /// Ramp: step by this fraction of the target rate: 0.02.
  rampStep(3),

  /// Ramp: minutes between steps: 2.
  rampMinutes(4),

  /// Interval: this fraction above, then below, the threshold: 0.15.
  intervalSpread(5),

  /// Interval: minutes in each period: 10.
  intervalMinutes(6),

  /// Tracking: ln(rate) moves by gain x (score - target) per check: 0.4.
  trackingGain(7),

  /// Plans: threshold gain an hour worth a whole unit of retention: 0.2.
  retentionCost(8),

  /// Threshold test: most presentations: 40.
  testMax(9),

  /// Threshold test: done when the 95% interval's high / low is below this: 1.25.
  testPrecision(10);

  const TrainerParam(this.value);

  /// The number the C library uses.
  final int value;
}

/// Throws [ArgumentError] if [value] is NaN; otherwise returns it.
double checkedNumber(double value, String name) {
  if (value.isNaN) throw ArgumentError.value(value, name, 'must be a number');
  return value;
}
