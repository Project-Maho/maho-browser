'use strict';

function validateMotionTelemetry(trace) {
  if (!trace || typeof trace !== 'object') {
    return { valid: false, reason: 'trace must be an object' };
  }

  const jitter = Number(trace.jitter_css_px);
  const overshoot = Number(trace.overshoot_px);
  const dwell = Number(trace.dwell_ms);

  const jitterBounded = !isNaN(jitter) && jitter >= 0 && jitter <= 0.8;
  const overshootBounded = !isNaN(overshoot) && overshoot >= 2.0 && overshoot <= 4.0;
  const dwellBounded = !isNaN(dwell) && dwell >= 80 && dwell <= 220;

  let movesMonotonic = true;
  if (Array.isArray(trace.moves) && trace.moves.length > 1) {
    for (let i = 1; i < trace.moves.length; i++) {
      if (trace.moves[i].t < trace.moves[i - 1].t) {
        movesMonotonic = false;
        break;
      }
    }
  }

  const valid = jitterBounded && overshootBounded && dwellBounded && movesMonotonic;

  return {
    valid: valid,
    jitter_bounded: jitterBounded,
    overshoot_bounded: overshootBounded,
    dwell_bounded: dwellBounded,
    moves_monotonic: movesMonotonic
  };
}

function validateKeyboardTelemetry(seq) {
  if (!Array.isArray(seq) || seq.length === 0) {
    return { valid: false, reason: 'sequence must be non-empty array' };
  }

  let ordered = true;
  for (let i = 1; i < seq.length; i++) {
    if (seq[i].t < seq[i - 1].t) {
      ordered = false;
      break;
    }
  }

  let modifiersPreserved = true;
  const activeDown = new Map();
  for (const event of seq) {
    if (event.type === 'keydown') {
      activeDown.set(event.code || event.key, event);
    } else if (event.type === 'keyup') {
      const down = activeDown.get(event.code || event.key);
      if (!down) {
        modifiersPreserved = false;
      }
      activeDown.delete(event.code || event.key);
    }
  }

  const valid = ordered && modifiersPreserved;

  return {
    valid: valid,
    ordered: ordered,
    modifiers_preserved: modifiersPreserved
  };
}

module.exports = {
  validateMotionTelemetry,
  validateKeyboardTelemetry
};
