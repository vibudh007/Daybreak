// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <WiFiClientSecure.h>
#include <stddef.h>
#include <stdint.h>

// Arduino's TLS client needs the embedded bundle attached explicitly to verify peers.
extern "C" {
extern const uint8_t embeddedCaBundleStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t embeddedCaBundleEnd[] asm("_binary_x509_crt_bundle_end");
}

namespace network {

inline bool configureTrustedCaBundle(WiFiClientSecure &client) {
    const size_t bundleSize = static_cast<size_t>(embeddedCaBundleEnd - embeddedCaBundleStart);
    if (bundleSize == 0) return false;
    client.setCACertBundle(embeddedCaBundleStart, bundleSize);
    return true;
}

}
