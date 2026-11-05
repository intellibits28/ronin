# 8. Security & Privacy Model

## Data Masking
The `ShmAiReviewPipeline` enforces strict data masking. By default, raw sensor arrays are completely filtered out of the JSON payloads before they are routed to any Cloud provider, ensuring sensitive location or mechanical fingerprinting data is not leaked.

## API Key Management
API keys for OpenRouter, OpenAI, or Gemini are stored locally. They are never logged to the native telemetry console and are dynamically injected into HTTPS request headers just-in-time.

## Sandboxing
The Native C++ kernel executes within the standard Android application sandbox. It cannot access files outside its designated data directories unless explicitly granted URI permissions via the Kotlin intent bridge. This prevents rogue AI plans from modifying critical system files.

## Capability Authorization & Lifecycle Governance
The `CapabilityDispatcher` implements strict policy enforcement using `CapabilityPolicyEngine`. Before any tool action is executed (e.g., SMS, Calendar), the request is validated against a statically loaded `policy_manifest.json`. Unlisted capabilities are denied by default. Additionally, all execution contexts are wrapped with guaranteed RAII cleanup to prevent budget leaks, and the `AgentScheduler` strictly propagates cancellation tokens to gracefully terminate tasks upon user cancellation or timeouts.

## Strict Safe Mode
When `Safe Mode` is toggled, all write operations to the `LongTermMemory` (including standard facts, episodes, and the secure vault) are immediately blocked. This provides an instant "read-only" lock for the entire cognitive state, ensuring data immutability during high-risk scenarios.

## Sovereign Local Research Engine 5-Layer Guardrails (v5.1)
1. **SSRF & DNS-Rebinding Blocker**: Blocks all loopback (`127.0.0.0/8`, `::1`), RFC1918 (`10/8`, `172.16/12`, `192.168/16`), CGNAT (`100.64.0.0/10`), Link-Local/Cloud Metadata (`169.254.0.0/16`, `fe80::/10`), IPv6 Unique Local (`fc00::/7`), and IPv4-mapped IPv6 (`::ffff:0:0/96`) addresses on initial connections and every HTTP redirect hop (`instanceFollowRedirects = false`).
2. **Stream Decompression Cap**: Enforces a hard `256 KB` decompressed stream limit and strict `text/html` / `text/plain` MIME validation to neutralize zip-bomb and OOM attacks.
3. **Indirect Prompt-Injection Sanitization**: Uses an $O(N)$ state-machine HTML distiller that strips `<script>`, `<style>`, HTML comments (`<!-- ... -->`), and hidden CSS (`display:none`, `visibility:hidden`, `opacity:0`, `font-size:0`, `aria-hidden="true"`), and wraps extracted text inside `<untrusted_web_content>` XML tags.
4. **Deterministic Cache Naming**: Saves cached research reports using `SHA-256(query).hex + ".md"` to prevent path traversal (`../`).
5. **Safe External Browser Intent**: Validates `https://` scheme before launching external browser intents.

## Authenticated Termux MCP Bridge (v5.1)
1. **Loopback Restriction**: Rejects non-loopback MCP endpoints (`127.0.0.1` / `localhost` only).
2. **256-bit Bearer + Per-Request HMAC-SHA256 Replay Protection**: Every JSON-RPC 2.0 call includes `Authorization: Bearer <secret>`, `X-Ronin-Timestamp: <epoch_sec>`, and `X-Ronin-Signature: HMAC_SHA256(secret, "$timestamp.$rawJsonBody")`, verified within a strict `±30s` window.
3. **Mandatory HITL Gate**: Every `tools/call` execution requires explicit user approval via the Android HITL confirmation dialog.
