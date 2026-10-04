'use strict';

const crypto = require('crypto');
const { CDPClient } = require('./cdp');

// Real scenario runner. Executes an ordered assertion list against a live CDP
// target and derives pass/fail from actual results. It never fabricates
// success: a scenario with no executed assertions is an ERROR, and any failed
// or throwing assertion fails the scenario.

function sha256(value) {
  const data = typeof value === 'string' ? value : JSON.stringify(value);
  return crypto.createHash('sha256').update(data).digest('hex');
}

// Run one assertion. `assertion.fn(ctx)` must return a truthy result object or
// throw. The returned evidence is hashed; throwing/false -> FAIL.
async function runAssertion(assertion, ctx) {
  try {
    const evidence = await assertion.fn(ctx);
    if (evidence === false || evidence === null || evidence === undefined) {
      return { id: assertion.id, status: 'FAIL', evidence_sha: sha256('') };
    }
    return {
      id: assertion.id,
      status: 'SUCCESS',
      evidence_sha: sha256(evidence),
    };
  } catch (err) {
    return {
      id: assertion.id,
      status: 'FAIL',
      evidence_sha: sha256(String(err && err.message ? err.message : err)),
    };
  }
}

// scenario = { name, browserWsUrl, expectedAssertionIds:[...],
//              setup?:async fn, actions?:async fn, assertions:[{id, fn}] }
async function runScenario(scenario) {
  const result = { scenario: scenario.name, status: 'ERROR', assertions: [] };

  if (!Array.isArray(scenario.assertions) || scenario.assertions.length === 0) {
    result.error = 'no assertions defined — nothing verified';
    return result;
  }

  let client = null;
  try {
    if (scenario.browserWsUrl) {
      client = new CDPClient(scenario.browserWsUrl);
      await client.connect();
    }
    const ctx = { client, fixture: scenario.fixture || {} };

    if (typeof scenario.setup === 'function') await scenario.setup(ctx);
    if (typeof scenario.actions === 'function') await scenario.actions(ctx);

    for (const assertion of scenario.assertions) {
      result.assertions.push(await runAssertion(assertion, ctx));
    }
  } catch (err) {
    result.error = String(err && err.message ? err.message : err);
    if (client) client.close();
    return result;
  }
  if (client) client.close();

  // Enforce that the exact expected assertion IDs were produced, in order.
  if (Array.isArray(scenario.expectedAssertionIds)) {
    const got = result.assertions.map((a) => a.id);
    const exp = scenario.expectedAssertionIds;
    const idsMatch =
      got.length === exp.length && got.every((id, i) => id === exp[i]);
    if (!idsMatch) {
      result.status = 'FAIL';
      result.error = `assertion id/order mismatch: expected ${JSON.stringify(
        exp
      )} got ${JSON.stringify(got)}`;
      return result;
    }
  }

  const allPass =
    result.assertions.length > 0 &&
    result.assertions.every((a) => a.status === 'SUCCESS');
  result.status = allPass ? 'SUCCESS' : 'FAIL';
  return result;
}

// Run a matrix of scenarios; overall SUCCESS only if every scenario succeeds.
async function runScenarios(scenarios) {
  const results = [];
  for (const scenario of scenarios) {
    results.push(await runScenario(scenario));
  }
  const overall = results.every((r) => r.status === 'SUCCESS')
    ? 'SUCCESS'
    : 'FAIL';
  return { overall, results };
}

module.exports = { runScenario, runScenarios, runAssertion, sha256 };
