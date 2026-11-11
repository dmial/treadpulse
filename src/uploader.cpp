#include "uploader.h"

#include <HTTPClient.h>
#include <LittleFS.h>
#include <NetworkClientSecure.h>

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

enum class Put { Ok, Conflict, Error };

// PUT <url>/walks/<walk>/<file> with the file as the body.
Put putFile(HTTPClient& http, NetworkClientSecure& client, const String& walk, const char* file) {
    const String path = Storage::walkFile(walk, file);
    File f = LittleFS.open(path, FILE_READ);
    if (!f) {
        Serial.printf("[upload] %s missing, skipped\n", path.c_str());
        return Put::Ok;
    }

    String base = secrets::kUploadUrl;
    while (base.endsWith("/")) base.remove(base.length() - 1);
    const String url = base + "/walks/" + walk + "/" + file;

    if (!http.begin(client, url)) {
        f.close();
        return Put::Error;
    }
    http.addHeader("Authorization", String("Bearer ") + secrets::kUploadToken);
    http.addHeader("Content-Type", "text/csv");
    const int code = http.sendRequest("PUT", &f, f.size());
    f.close();
    const String body = code > 0 ? http.getString() : String();
    http.end();

    if (code == 200 || code == 201) return Put::Ok;
    Serial.printf("[upload] %s/%s: %d %s\n", walk.c_str(), file,
                  code, code > 0 ? body.c_str() : HTTPClient::errorToString(code).c_str());
    // 409: the cloud already holds a different file under this name.
    return code == 409 ? Put::Conflict : Put::Error;
}

}  // namespace

Result sendPending(Storage& storage, uint32_t budgetMs) {
    Result result;
    if (strlen(secrets::kUploadUrl) == 0 || strlen(secrets::kUploadToken) == 0) {
        Serial.println("[upload] no receiver in secrets.h, walks stay in flash");
        result.failed = true;
        return result;
    }

    NetworkClientSecure client;
    client.setCACertBundle(kCaBundleStart, kCaBundleEnd - kCaBundleStart);
    HTTPClient http;
    http.setReuse(true);  // one TLS handshake for all files
    http.setConnectTimeout(kConnectTimeoutMs);
    http.setTimeout(kResponseTimeoutMs);

    const uint32_t start = millis();
    for (const String& walk : storage.pendingWalks()) {
        if (millis() - start >= budgetMs) break;

        bool walkOk = true;
        for (const char* file : Storage::kWalkFiles) {
            const Put put = putFile(http, client, walk, file);
            if (put == Put::Ok) continue;
            walkOk = false;
            if (put == Put::Error) result.failed = true;
            break;
        }
        if (result.failed) break;  // network or receiver trouble: retry later
        if (!walkOk) continue;     // name conflict: keep this walk, try the next one

        storage.removeWalk(walk);
        ++result.sent;
        Serial.printf("[upload] walk %s sent\n", walk.c_str());
    }
    return result;
}

}  // namespace uploader
