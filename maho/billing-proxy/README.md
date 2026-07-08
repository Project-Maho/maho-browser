## Local development

### Prerequisites
- Rust with `wasm32-unknown-unknown` target: `rustup target add wasm32-unknown-unknown`
- Wrangler v4.62.0+: `bun install -g wrangler` (or comparable)
- An OpenAI-compatible upstream running locally (Ollama, Quotio, LiteLLM, vLLM, etc.)

### 1. Start relay
    cd maho/relay
    export MAHO_RELAY_SERVICE_TOKEN=$(openssl rand -hex 32)
    export MAHO_RELAY_DB_PATH=/tmp/maho-relay-dev.db
    export MAHO_RELAY_PORT=13000
    export MAHO_RELAY_OPEN_REGISTRATION=true   # dev only; leave closed in prod
    cargo run -p maho-relay
    # Listens on http://localhost:13000

### 2. Create a test account and capture access token
    curl -X POST http://localhost:13000/auth/signup \
      -H "Content-Type: application/json" \
      -d '{"email":"dev@maho.local","password":"dev-test-password-12chars",
           "display_name":"Dev","device_name":"Dev Laptop","device_type":"desktop"}'
    # Response contains access_token (Bearer) and refresh_token.

### 3. Configure billing-proxy secrets
    cd maho/billing-proxy
    cp .dev.vars.example .dev.vars
    # Edit .dev.vars — set SERVICE_TOKEN to the same value as MAHO_RELAY_SERVICE_TOKEN
    # Set UPSTREAM_LLM_URL to your local upstream (e.g. http://127.0.0.1:8317/v1/chat/completions)
    # Set OPENROUTER_API_KEY to whatever key your upstream expects (any string for Ollama)

### 4. Seed KV allowlist for local testing
    # Key format: bare tier name (`free`/`pro`/`max`). Value: newline-separated model IDs.
    wrangler kv key put --local --binding=MAHO_MODEL_ALLOWLIST free "$(printf 'model-id-1\nmodel-id-2\n')"

### 5. Start billing-proxy
    wrangler dev --local --port 18787
    # First run compiles Rust→wasm32 (~15-30s). Subsequent runs cached.
    # Listens on http://localhost:18787

### 6. End-to-end test
    curl -X POST http://localhost:18787/v1/chat/completions \
      -H "Authorization: Bearer <access_token_from_step_2>" \
      -H "Content-Type: application/json" \
      -d '{"model":"<one-from-allowlist>","messages":[{"role":"user","content":"ping"}],
           "max_tokens":10,"stream":false}'
    # Expect 200 OK with OpenAI-shape response.

### 7. Point browser client at local billing-proxy
    export MAHO_PROXY_URL=http://localhost:18787/v1
    python3 maho-chromium/build/scripts/build_maho.py  # then run Maho.app
