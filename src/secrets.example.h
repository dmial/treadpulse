// Template: copy this file to src/secrets.h and fill it in, then reflash.
// src/secrets.h is git-ignored, so the Wi-Fi password and the upload key never
// get committed. They do end up inside the firmware on the board.
#pragma once

namespace secrets {

// Home Wi-Fi (2.4 GHz only). Used between walks to set the clock and upload walks.
constexpr const char* kWifiSsid = "";
constexpr const char* kWifiPassword = "";

// Cloud receiver (cloud/ in this repo), without a trailing slash,
// e.g. "https://treadmill-upload.<your-subdomain>.workers.dev".
constexpr const char* kUploadUrl = "";

// Write-only key of the receiver: the same value as its UPLOAD_TOKEN secret.
constexpr const char* kUploadToken = "";

}  // namespace secrets
