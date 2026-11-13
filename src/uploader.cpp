#include "uploader.h"

#include <HTTPClient.h>
#include <LittleFS.h>
#include <NetworkClientSecure.h>

#include "board_log.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "src/secrets.h is missing: copy src/secrets.example.h to src/secrets.h and fill it in"
#endif

// Root certificates of the common public CAs, built into the ESP-IDF libraries.
extern const uint8_t kCaBundleStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t kCaBundleEnd[] asm("_binary_x509_crt_bundle_end");

namespace uploader {

namespace {

constexpr uint32_t kConnectTimeoutMs = 10000;
constexpr uint32_t kResponseTimeoutMs = 20000;
// The receiver keeps one copy of the log under this name (cloud/src/index.ts).
constexpr const char* kLogPath = "logs/board.txt";

enum class Put { Ok, Conflict, Error };

bool configured() {
    if (strlen(secrets::kUploadUrl) > 0 && strlen(secrets::kUploadToken) > 0) return true;
    boardlog::println("[upload] no receiver in secrets.h, nothing is sent");
    return false;
}

// One TLS connection, reused for every request through it.
struct Connection {
    NetworkClientSecure client;
    HTTPClient http;

    Connection() {
        client.setCACertBundle(kCaBundleStart, kCaBundleEnd - kCaBundleStart);
        http.setReuse(true);
        http.setConnectTimeout(kConnectTimeoutMs);
        http.setTimeout(kResponseTimeoutMs);
    }

    // Opens a PUT to <receiver>/<path> with the key; false if the address is bad.
    bool beginPut(const String& path, const char* contentType) {
        String base = secrets::kUploadUrl;
        while (base.endsWith("/")) base.remove(base.length() - 1);
        if (!http.begin(client, base + "/" + path)) return false;
        http.addHeader("Authorization", String("Bearer ") + secrets::kUploadToken);
        http.addHeader("Content-Type", contentType);
        return true;
    }

    // Closes the request; logs it when the receiver did not take the file.
    Put finish(int code, const String& what) {
        const String body = code > 0 ? http.getString() : String();
        http.end();
        if (code == 200 || code == 201) return Put::Ok;
        boardlog::printf("[upload] %s: %d %s\n", what.c_str(), code,
                         code > 0 ? body.c_str() : HTTPClient::errorToString(code).c_str());
        // 409: the cloud already holds a different file under this name.
        return code == 409 ? Put::Conflict : Put::Error;
    }
};

// PUT <url>/walks/<walk>/<file> with the file as the body.
Put putWalkFile(Connection& conn, const String& walk, const char* file) {
    const String path = Storage::walkFile(walk, file);
    File f = LittleFS.open(path, FILE_READ);
    if (!f) {
        boardlog::printf("[upload] %s missing, skipped\n", path.c_str());
        return Put::Ok;
    }
    if (!conn.beginPut("walks/" + walk + "/" + file, "text/csv")) {
        f.close();
        return Put::Error;
    }
    const int code = conn.http.sendRequest("PUT", &f, f.size());
    f.close();
    return conn.finish(code, walk + "/" + file);
}

}  // namespace

Result sendPending(Storage& storage, uint32_t budgetMs) {
    Result result;
    if (!configured()) {
        result.failed = true;
        return result;
    }

    Connection conn;
    const uint32_t start = millis();
    for (const String& walk : storage.pendingWalks()) {
        if (millis() - start >= budgetMs) break;

        bool walkOk = true;
        for (const char* file : Storage::kWalkFiles) {
            const Put put = putWalkFile(conn, walk, file);
            if (put == Put::Ok) continue;
            walkOk = false;
            if (put == Put::Error) result.failed = true;
            break;
        }
        if (result.failed) break;  // network or receiver trouble: retry later
        if (!walkOk) continue;     // name conflict: keep this walk, try the next one

        storage.removeWalk(walk);
        ++result.sent;
        boardlog::printf("[upload] walk %s sent\n", walk.c_str());
    }
    return result;
}

bool sendLog() {
    if (!configured()) return false;
    boardlog::flush();
    size_t size = 0;
    boardlog::Buffer log = boardlog::readAll(size);
    if (size == 0) return true;

    Connection conn;
    if (!conn.beginPut(kLogPath, "text/plain; charset=utf-8")) return false;
    const int code = conn.http.sendRequest("PUT", log.get(), size);
    return conn.finish(code, "log") == Put::Ok;
}

}  // namespace uploader
