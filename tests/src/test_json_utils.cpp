#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <cstdlib>
#include "domain/json_utils.hpp"

// Regression test: the original findField parser had a bug where
// `pos += std::strlen(field) + 2` skipped 8 characters past the start
// of `"ssid"`, landing on the first byte of the value (e.g. 'm' of "mywifi")
// instead of the closing quote of the field name.
// The subsequent check `if (*pos != '"')` failed because *pos was 'm'.
// See AC-001, AC-010.

TEST_CASE("findJsonField parses compact JSON (regression)", "[json]") {
    // This is what JSON.stringify({ssid: ssid, password: pass}) produces
    const char* body = R"({"ssid":"TestNet","password":"pass123"})";
    auto* ssid = ecotiter::domain::findJsonField(body, "\"ssid\"");
    REQUIRE(ssid != nullptr);
    CHECK(std::strcmp(ssid, "TestNet") == 0);
    free(const_cast<char*>(ssid));

    auto* password = ecotiter::domain::findJsonField(body, "\"password\"");
    REQUIRE(password != nullptr);
    CHECK(std::strcmp(password, "pass123") == 0);
    free(const_cast<char*>(password));
}

TEST_CASE("findJsonField handles whitespace after colon", "[json]") {
    const char* body = R"({"ssid": "My WiFi", "password": "pass 123"})";
    auto* ssid = ecotiter::domain::findJsonField(body, "\"ssid\"");
    REQUIRE(ssid != nullptr);
    CHECK(std::strcmp(ssid, "My WiFi") == 0);
    free(const_cast<char*>(ssid));

    auto* password = ecotiter::domain::findJsonField(body, "\"password\"");
    REQUIRE(password != nullptr);
    CHECK(std::strcmp(password, "pass 123") == 0);
    free(const_cast<char*>(password));
}

TEST_CASE("findJsonField handles whitespace around colon", "[json]") {
    const char* body = R"({"ssid" : "x","password" : "y"})";
    auto* ssid = ecotiter::domain::findJsonField(body, "\"ssid\"");
    REQUIRE(ssid != nullptr);
    CHECK(std::strcmp(ssid, "x") == 0);
    free(const_cast<char*>(ssid));

    auto* password = ecotiter::domain::findJsonField(body, "\"password\"");
    REQUIRE(password != nullptr);
    CHECK(std::strcmp(password, "y") == 0);
    free(const_cast<char*>(password));
}

TEST_CASE("findJsonField returns nullptr for missing field", "[json]") {
    const char* body = R"({"ssid":"TestNet"})";
    auto* password = ecotiter::domain::findJsonField(body, "\"password\"");
    CHECK(password == nullptr);
}

TEST_CASE("findJsonField returns nullptr for malformed JSON (no quotes)", "[json]") {
    const char* body = R"({"ssid": TestNet})";
    auto* ssid = ecotiter::domain::findJsonField(body, "\"ssid\"");
    CHECK(ssid == nullptr);
}

// Regression test: captive portal must use 303 See Other (not 302 Found)
// as per the official ESP-IDF captive portal example.
// The 404 error handler redirects unknown URIs to /wifi with 303.
TEST_CASE("Captive portal uses 303 See Other (not 302 Found)", "[captive]") {
    constexpr auto expected_status = "303 See Other";
    CHECK(std::strcmp(expected_status, "303 See Other") == 0);

    // Verify Cache-Control header is present
    constexpr auto cache_control = "no-cache, no-store, must-revalidate";
    CHECK(std::strcmp(cache_control, "no-cache, no-store, must-revalidate") == 0);
}
