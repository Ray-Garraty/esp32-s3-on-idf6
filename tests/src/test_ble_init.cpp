// Regression test: verify BLE init sequence source code patterns.
// These patterns MUST be present in ble.cpp after the bugfix (BLE advertising bugfix).
// Since NimBLE headers are not available in the host test build,
// we verify the source code patterns directly via file I/O.
//
// This is a behavior test — we test that the init sequence has the required
// properties (GAP/GATT init after nimble_port_init, error checking on
// ble_gatts_count_cfg/ble_gatts_add_svcs, scan response with NUS UUID).

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <string>
#include <vector>

// Absolute path via CMake compile definition — works from any CWD
static constexpr auto BLE_SRC_PATH = TESTS_SOURCE_DIR "/../components/infrastructure/network/src/ble.cpp";

static std::vector<std::string> readFileLines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream file(path);
    if (!file.is_open()) {
        // If file can't be opened, return empty — tests will fail with clear message
        return lines;
    }
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    return lines;
}

TEST_CASE("BLE src includes GAP and GATT service headers", "[ble][regression]") {
    auto lines = readFileLines(BLE_SRC_PATH);
    REQUIRE_FALSE(lines.empty());

    bool hasGapInclude  = false;
    bool hasGattInclude = false;
    for (const auto& line : lines) {
        if (line.find("#include \"services/gap/ble_svc_gap.h\"") != std::string::npos) {
            hasGapInclude = true;
        }
        if (line.find("#include \"services/gatt/ble_svc_gatt.h\"") != std::string::npos) {
            hasGattInclude = true;
        }
    }
    INFO("Checking includes in: " << BLE_SRC_PATH);
    REQUIRE(hasGapInclude);
    REQUIRE(hasGattInclude);
}

TEST_CASE("BLE init calls ble_svc_gap_init and ble_svc_gatt_init after nimble_port_init",
          "[ble][regression]") {
    auto lines = readFileLines(BLE_SRC_PATH);
    REQUIRE_FALSE(lines.empty());

    size_t nimbleDoneLine = 0;
    size_t gapInitLine    = 0;
    size_t gattInitLine   = 0;

    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].find("nimble_port_init done") != std::string::npos) {
            nimbleDoneLine = i;
        }
        if (lines[i].find("ble_svc_gap_init()") != std::string::npos) {
            gapInitLine = i;
        }
        if (lines[i].find("ble_svc_gatt_init()") != std::string::npos) {
            gattInitLine = i;
        }
    }

    // Verify all markers found
    REQUIRE(nimbleDoneLine > 0);
    REQUIRE(gapInitLine > 0);
    REQUIRE(gattInitLine > 0);

    // Verify order: nimble_port_init -> gap_init -> gatt_init
    REQUIRE(gapInitLine > nimbleDoneLine);
    REQUIRE(gattInitLine > nimbleDoneLine);
}

TEST_CASE("BLE init checks return values of ble_gatts_count_cfg and ble_gatts_add_svcs",
          "[ble][regression]") {
    auto lines = readFileLines(BLE_SRC_PATH);
    REQUIRE_FALSE(lines.empty());

    bool hasCountErrorCheck = false;
    bool hasAddErrorCheck   = false;

    for (const auto& line : lines) {
        if (line.find("ble_gatts_count_cfg failed") != std::string::npos) {
            hasCountErrorCheck = true;
        }
        if (line.find("ble_gatts_add_svcs failed") != std::string::npos) {
            hasAddErrorCheck = true;
        }
    }

    REQUIRE(hasCountErrorCheck);
    REQUIRE(hasAddErrorCheck);
}

TEST_CASE("BLE advertising has scan response with NUS service UUID", "[ble][regression]") {
    auto lines = readFileLines(BLE_SRC_PATH);
    REQUIRE_FALSE(lines.empty());

    bool hasRspFieldsDecl  = false;
    bool hasRspSetFields   = false;
    bool hasNusInRsp       = false;

    for (const auto& line : lines) {
        if (line.find("ble_hs_adv_fields rsp_fields") != std::string::npos) {
            hasRspFieldsDecl = true;
        }
        if (line.find("ble_gap_adv_rsp_set_fields") != std::string::npos) {
            hasRspSetFields = true;
        }
        if (line.find("NUS_SVC_UUID") != std::string::npos &&
            line.find("rsp_fields.uuids128") != std::string::npos) {
            hasNusInRsp = true;
        }
    }

    REQUIRE(hasRspFieldsDecl);
    REQUIRE(hasRspSetFields);
    REQUIRE(hasNusInRsp);
}
