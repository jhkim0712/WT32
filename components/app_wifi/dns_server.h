/**
 * @file dns_server.h
 * @brief Minimal captive-portal DNS server: answers every A query with a
 *        fixed IPv4 address (the SoftAP's own address).
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start the DNS hijack task. @param resolved_ip IPv4 address in network byte order. */
esp_err_t dns_server_start(uint32_t resolved_ip);

/** Stop the DNS hijack task and close its socket. */
void dns_server_stop(void);

#ifdef __cplusplus
}
#endif
