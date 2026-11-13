// Receives walk logs from the treadmill display and stores them in R2.
//
//   PUT /walks/<walk>/<file>      Authorization: Bearer <UPLOAD_TOKEN>
//   <walk>  e.g. 0042_2026-11-10T0815Z or 0042_nodate_9f3a1c2e
//   <file>  samples.csv, segments.csv or summary.csv
//
//   PUT /logs/board.txt           the board's own log (src/board_log.h)
//
// Write-only: nothing can be read back through this Worker, so the key baked
// into the board's firmware cannot expose stored data. A stored walk file is
// never overwritten: the same content again answers 200 (a retried upload),
// different content answers 409. The log is one file that each upload
// replaces: the board always sends all of it.

export interface Env {
  WALKS: R2Bucket;
  UPLOAD_TOKEN: string;
}

const KEY = /^walks\/\d{4,}_[0-9A-Za-z_-]+\/(samples|segments|summary)\.csv$/;
// A 3-hour walk is ~750 KB. Kept tight: R2 has no spending cap, so this bounds
// what a leaked key could store.
const MAX_BYTES = 4 * 1024 * 1024;
const LOG_KEY = "logs/board.txt";
// The board keeps at most 64 KB of log; some room above that.
const MAX_LOG_BYTES = 256 * 1024;

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    if (request.method !== "PUT") return reply(405, "only PUT is accepted", { Allow: "PUT" });
    if (!(await authorized(request, env))) return reply(401, "wrong or missing token");

    const key = decodeURIComponent(new URL(request.url).pathname.slice(1));
    if (key === LOG_KEY) return storeLog(request, env);
    if (!KEY.test(key)) return reply(400, "unexpected path");

    const declared = Number(request.headers.get("Content-Length") ?? "0");
    if (declared > MAX_BYTES) return reply(413, "file too large");
    const body = await request.arrayBuffer();
    if (body.byteLength > MAX_BYTES) return reply(413, "file too large");

    const sha256 = toHex(await crypto.subtle.digest("SHA-256", body));
    const existing = await env.WALKS.head(key);
    if (existing) {
      return existing.customMetadata?.sha256 === sha256
        ? reply(200, "already stored")
        : reply(409, "a different file is already stored under this name");
    }

    await env.WALKS.put(key, body, {
      httpMetadata: { contentType: "text/csv; charset=utf-8" },
      customMetadata: { sha256 },
    });
    return reply(201, "stored");
  },
} satisfies ExportedHandler<Env>;

async function storeLog(request: Request, env: Env): Promise<Response> {
  const declared = Number(request.headers.get("Content-Length") ?? "0");
  if (declared > MAX_LOG_BYTES) return reply(413, "log too large");
  const body = await request.arrayBuffer();
  if (body.byteLength > MAX_LOG_BYTES) return reply(413, "log too large");
  await env.WALKS.put(LOG_KEY, body, {
    httpMetadata: { contentType: "text/plain; charset=utf-8" },
  });
  return reply(200, "log stored");
}

async function authorized(request: Request, env: Env): Promise<boolean> {
  const header = request.headers.get("Authorization") ?? "";
  const given = header.startsWith("Bearer ") ? header.slice("Bearer ".length) : "";
  if (!given || !env.UPLOAD_TOKEN) return false;
  // Compare digests: equal length, and the comparison takes constant time.
  const encoder = new TextEncoder();
  const [a, b] = await Promise.all([
    crypto.subtle.digest("SHA-256", encoder.encode(given)),
    crypto.subtle.digest("SHA-256", encoder.encode(env.UPLOAD_TOKEN)),
  ]);
  return crypto.subtle.timingSafeEqual(a, b);
}

function toHex(buffer: ArrayBuffer): string {
  return [...new Uint8Array(buffer)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

function reply(status: number, message: string, headers: Record<string, string> = {}): Response {
  return new Response(message + "\n", {
    status,
    headers: { "Content-Type": "text/plain; charset=utf-8", ...headers },
  });
}
