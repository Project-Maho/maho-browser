const check = (taskId, namedChecks = []) => Object.freeze({ taskIds: Object.freeze([taskId]), namedChecks: Object.freeze(namedChecks) });

export const NAMED_CHECKS_V1 = Object.freeze({
  "hero-html-proof": "T07",
  "agent-boundaries": "T12",
  "source-guards": "T16",
  "scope-fidelity": "T01",
  "capture-readiness": "T03",
});

export const PLAN_CHECKS_V1 = Object.freeze({
  M001: check("T07", ["hero-html-proof"]), M002: check("T07"), M003: check("T05"), M004: check("T07"),
  M005: check("T07"), M006: check("T02"), M007: check("T07"), M008: check("T07"),
  M009: check("T07"), M010: check("T08"), M011: check("T08"), M012: check("T06"),
  M013: check("T06"), M014: check("T06"), M015: check("T06"), M016: check("T06"),
  M017: check("T06"), M018: check("T06"), M019: check("T02"), M020: check("T02"),
  M021: check("T02"), M022: check("T05"), M023: check("T05"), M024: check("T06"),
  M025: check("T06"), M026: check("T06"), M027: check("T05"), M028: check("T05"),
  M029: check("T05"), M030: check("T05"), M031: check("T05"), M032: check("T06"),
  M033: check("T06"), M034: check("T06"), M035: check("T06"), M036: check("T05"),
  M037: check("T05"), M038: check("T05"), M039: check("T05"), M040: check("T05"),
  M041: check("T05"), M042: check("T05"), M043: check("T05"), M044: check("T05"),
  M045: check("T05"), M046: check("T05"), M047: check("T10"), M048: check("T05"),
  M049: check("T02"), M050: check("T02"), M051: check("T01"), M052: check("T05"),
  M053: check("T05"), M054: check("T05"), M055: check("T05"), M056: check("T10"),
  M057: check("T11"), M058: check("T04"), M059: check("T12", ["agent-boundaries"]), M060: check("T12"),
  M061: check("T07"), M062: check("T04"), M063: check("T02"), M064: check("T14"),
  M065: check("T14"), M066: check("T03", ["capture-readiness"]), M067: check("T01"), M068: check("T01", ["scope-fidelity"]),
  M069: check("T06"), M070: check("T01"), M071: check("T01"), M072: check("T01"),
  M073: check("T01"), M074: check("T01"), M075: check("T01"), M076: check("T01"),
  M077: check("T01"), M078: check("T01"), M079: check("T01"), M080: check("T01"),
  M081: check("T01"), M082: check("T01"),
  N001: check("T01"), N002: check("T01"), N003: check("T05"), N004: check("T07"),
  N005: check("T07"), N006: check("T01"), N007: check("T05"), N008: check("T05"),
  N009: check("T05"), N010: check("T05"), N011: check("T14"), N012: check("T02"),
  N013: check("T01"), N014: check("T07"), N015: check("T08"), N016: check("T01"),
  N017: check("T07"), N018: check("T06", ["source-guards"]),
});
