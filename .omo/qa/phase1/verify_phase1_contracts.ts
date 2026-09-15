
import assert from "node:assert/strict";

console.log("=== PHASE 1 CONTRACT TESTS ===");

// 1. Task enum & mapping
const TASKS = ["chat", "tab_tidy", "inline_edit", "page_preview", "tab_title", "download_tidy", "memory"] as const;
type Task = typeof TASKS[number];

const PROVIDERS = ["maho-managed", "openai", "anthropic", "openai-compatible", "local-server"] as const;
type Provider = typeof PROVIDERS[number];

// Mock PrefService
class MockPrefs {
  data = new Map<string, any>();

  constructor() {
    this.data.set("maho.ai.provider", "");
    this.data.set("maho.ai.model", "gpt-4o-mini");
    this.data.set("maho.ai.base_url", "");
    this.data.set("maho.ai.api_key", "");
    this.data.set("maho.ai.byok.openai.encrypted_b64", "");
    this.data.set("maho.ai.byok.anthropic.encrypted_b64", "");
    this.data.set("maho.ai.provider_configs", {});
    this.data.set("maho.ai.task_models", {});
    this.data.set("maho.ai.byok.openai_compatible.encrypted_b64", "");
    this.data.set("maho.ai.models_settings_migrated_v1", false);
    this.data.set("maho.ai.settings_migrated_from_google_provider", false);
    this.data.set("maho.ai.model_cache", {});
    this.data.set("maho.account.relay_access_token_encrypted_b64", "");
  }

  getString(k: string): string { return this.data.get(k) ?? ""; }
  setString(k: string, v: string) { this.data.set(k, v); }
  getBool(k: string): boolean { return this.data.get(k) ?? false; }
  setBool(k: string, v: boolean) { this.data.set(k, v); }
  getDict(k: string): Record<string, any> { return JSON.parse(JSON.stringify(this.data.get(k) ?? {})); }
  setDict(k: string, v: Record<string, any>) { this.data.set(k, JSON.parse(JSON.stringify(v))); }
  clearPref(k: string) { this.data.set(k, ""); }
}

function isProviderConnected(prefs: MockPrefs, provider: Provider): boolean {
  if (provider === "maho-managed") return Boolean(prefs.getString("maho.account.relay_access_token_encrypted_b64"));
  if (provider === "openai") return Boolean(prefs.getString("maho.ai.byok.openai.encrypted_b64"));
  if (provider === "anthropic") return Boolean(prefs.getString("maho.ai.byok.anthropic.encrypted_b64"));
  if (provider === "openai-compatible") {
    const cfgs = prefs.getDict("maho.ai.provider_configs");
    return Boolean(cfgs["openai-compatible"]?.base_url || prefs.getString("maho.ai.base_url"));
  }
  if (provider === "local-server") {
    const cfgs = prefs.getDict("maho.ai.provider_configs");
    return Boolean(cfgs["local-server"]?.base_url || prefs.getString("maho.ai.base_url"));
  }
  return false;
}

function resolveRoute(prefs: MockPrefs, task: Task): { ok: boolean; error?: string; provider?: string; model?: string; inherited?: boolean } {
  // 1. If override-capable task and task_models has entry:
  if (task !== "chat") {
    const tm = prefs.getDict("maho.ai.task_models");
    if (tm[task]?.provider && tm[task]?.model) {
      const p = tm[task].provider as Provider;
      const m = tm[task].model;
      if (!PROVIDERS.includes(p)) return { ok: false, error: "kUnknownProvider" };
      if (!isProviderConnected(prefs, p)) return { ok: false, error: "kProviderDisconnected" };
      return { ok: true, provider: p, model: m, inherited: false };
    }
  }

  // 2. Resolve Default:
  const dp = prefs.getString("maho.ai.provider") as Provider;
  const dm = prefs.getString("maho.ai.model");

  if (!dp) {
    if (prefs.getString("maho.account.relay_access_token_encrypted_b64")) {
      return { ok: true, provider: "maho-managed", model: dm || "google/gemini-3-flash-lite:free", inherited: true };
    }
    return { ok: false, error: "kNoDefault" };
  }

  if (!PROVIDERS.includes(dp)) return { ok: false, error: "kUnknownProvider" };
  if (!isProviderConnected(prefs, dp)) return { ok: false, error: "kProviderDisconnected" };
  if (!dm) return { ok: false, error: "kModelMissing" };

  return { ok: true, provider: dp, model: dm, inherited: task !== "chat" };
}

function migrateV1(prefs: MockPrefs, encryptorAvailable: boolean): boolean {
  if (prefs.getBool("maho.ai.models_settings_migrated_v1")) return true;

  // Google migration
  if (!prefs.getBool("maho.ai.settings_migrated_from_google_provider")) {
    if (prefs.getString("maho.ai.provider") === "google") {
      prefs.setString("maho.ai.provider", "");
    }
    prefs.setBool("maho.ai.settings_migrated_from_google_provider", true);
  }

  const lp = prefs.getString("maho.ai.provider");
  const lm = prefs.getString("maho.ai.model");

  // provider_configs
  const cfgs = prefs.getDict("maho.ai.provider_configs");
  if (lp && lm) {
    cfgs[lp] = cfgs[lp] || {};
    cfgs[lp].last_model = lm;
  }
  if (lp === "openai-compatible") {
    cfgs["openai-compatible"] = cfgs["openai-compatible"] || {};
    cfgs["openai-compatible"].base_url = prefs.getString("maho.ai.base_url");
  } else if (lp === "local-server") {
    cfgs["local-server"] = cfgs["local-server"] || {};
    cfgs["local-server"].base_url = prefs.getString("maho.ai.base_url");
  }
  prefs.setDict("maho.ai.provider_configs", cfgs);

  // Encrypted key
  if (lp === "openai-compatible" && prefs.getString("maho.ai.api_key")) {
    if (!encryptorAvailable) return false;
    prefs.setString("maho.ai.byok.openai_compatible.encrypted_b64", "b64(" + prefs.getString("maho.ai.api_key") + ")");
    prefs.clearPref("maho.ai.api_key");
  }

  prefs.setBool("maho.ai.models_settings_migrated_v1", true);
  return true;
}

// RUN TESTS
{
  const p = new MockPrefs();
  p.setString("maho.ai.byok.openai.encrypted_b64", "enc-openai");
  p.setString("maho.ai.provider", "openai");
  p.setString("maho.ai.model", "gpt-4o");

  // Chat resolves default
  const rChat = resolveRoute(p, "chat");
  assert.equal(rChat.ok, true);
  assert.equal(rChat.provider, "openai");
  assert.equal(rChat.inherited, false);

  // TabTidy inherits default
  const rTidy = resolveRoute(p, "tab_tidy");
  assert.equal(rTidy.ok, true);
  assert.equal(rTidy.provider, "openai");
  assert.equal(rTidy.inherited, true);

  // Override TabTidy to Anthropic
  p.setString("maho.ai.byok.anthropic.encrypted_b64", "enc-anthropic");
  const tm = p.getDict("maho.ai.task_models");
  tm["tab_tidy"] = { provider: "anthropic", model: "claude-3-5-sonnet" };
  p.setDict("maho.ai.task_models", tm);

  const rTidyOver = resolveRoute(p, "tab_tidy");
  assert.equal(rTidyOver.ok, true);
  assert.equal(rTidyOver.provider, "anthropic");
  assert.equal(rTidyOver.inherited, false);

  // Disconnecting Anthropic fails closed for TabTidy, no fallback to OpenAI
  p.setString("maho.ai.byok.anthropic.encrypted_b64", "");
  const rTidyFail = resolveRoute(p, "tab_tidy");
  assert.equal(rTidyFail.ok, false);
  assert.equal(rTidyFail.error, "kProviderDisconnected");

  // Chat still works
  assert.equal(resolveRoute(p, "chat").ok, true);
  console.log("✓ Route resolver contract verified");
}

// MIGRATION TESTS
{
  const p = new MockPrefs();
  p.setString("maho.ai.provider", "openai-compatible");
  p.setString("maho.ai.base_url", "http://127.0.0.1:18801");
  p.setString("maho.ai.model", "gpt-oss-120b-medium");
  p.setString("maho.ai.api_key", "raw-secret-123");

  // Without encryptor -> returns false, key preserved
  assert.equal(migrateV1(p, false), false);
  assert.equal(p.getString("maho.ai.api_key"), "raw-secret-123");
  assert.equal(p.getBool("maho.ai.models_settings_migrated_v1"), false);

  // With encryptor -> encrypts key, clears plaintext, completes
  assert.equal(migrateV1(p, true), true);
  assert.equal(p.getBool("maho.ai.models_settings_migrated_v1"), true);
  assert.equal(p.getString("maho.ai.api_key"), "");
  assert.equal(p.getString("maho.ai.byok.openai_compatible.encrypted_b64"), "b64(raw-secret-123)");
  assert.equal(p.getDict("maho.ai.provider_configs")["openai-compatible"].base_url, "http://127.0.0.1:18801");
  assert.equal(p.getDict("maho.ai.provider_configs")["openai-compatible"].last_model, "gpt-oss-120b-medium");
  console.log("✓ Migration contract verified");
}

console.log("ALL 100% PASS");
