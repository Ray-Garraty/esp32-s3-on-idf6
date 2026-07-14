#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "infrastructure/config.hpp"
#include "domain/memory.hpp"

namespace ecotiter::infrastructure::network {
class BleManager;
class HttpServer;
}

struct NetTaskParams {
    ecotiter::infrastructure::network::BleManager* bleManager;
};

extern std::atomic<ecotiter::infrastructure::network::HttpServer*> gHttpServerForWs;

struct WsSendEntry {
    char data[ecotiter::config::WS_BUF_SIZE];
    size_t len;
};

struct WsBroadcastEntry {
    char data[ecotiter::domain::memory::MAX_RSP_SIZE];
    size_t len;
};

extern QueueHandle_t gWsSendQueue;
extern QueueHandle_t gWsBroadcastQueue;

extern "C" void netTaskEntry(void* pvParameters);
