#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "dns_server.h"

static const char *TAG = "dns_server";

#define DNS_PORT     53
#define DNS_BUF_LEN  512

typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;

static TaskHandle_t s_task = NULL;
static int s_sock = -1;
static uint32_t s_resolved_ip;
static volatile bool s_running = false;

static void dns_server_task(void *arg)
{
    uint8_t rx_buf[DNS_BUF_LEN];
    uint8_t tx_buf[DNS_BUF_LEN];

    while (s_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int len = recvfrom(s_sock, rx_buf, sizeof(rx_buf), 0, (struct sockaddr *)&client_addr, &addr_len);
        if (len < (int)sizeof(dns_header_t)) {
            continue;
        }

        /* Locate the end of the (single) question section: a chain of
         * length-prefixed labels terminated by a 0 byte, followed by QTYPE
         * and QCLASS (2 bytes each). */
        int pos = sizeof(dns_header_t);
        while (pos < len && rx_buf[pos] != 0) {
            pos += rx_buf[pos] + 1;
        }
        int question_end = pos + 1 + 4;
        if (question_end > len || question_end > (int)sizeof(tx_buf) - 16) {
            continue;
        }

        memcpy(tx_buf, rx_buf, question_end);
        dns_header_t *resp_hdr = (dns_header_t *)tx_buf;
        resp_hdr->flags = htons(0x8180); /* standard query response, no error */
        resp_hdr->qdcount = htons(1);
        resp_hdr->ancount = htons(1);
        resp_hdr->nscount = 0;
        resp_hdr->arcount = 0;

        int resp_len = question_end;
        static const uint8_t answer_prefix[] = {
            0xC0, 0x0C,             /* name: pointer back to the question's QNAME */
            0x00, 0x01,             /* TYPE = A */
            0x00, 0x01,             /* CLASS = IN */
            0x00, 0x00, 0x00, 0x3C, /* TTL = 60s */
            0x00, 0x04,             /* RDLENGTH = 4 */
        };
        memcpy(tx_buf + resp_len, answer_prefix, sizeof(answer_prefix));
        resp_len += sizeof(answer_prefix);
        memcpy(tx_buf + resp_len, &s_resolved_ip, 4);
        resp_len += 4;

        sendto(s_sock, tx_buf, resp_len, 0, (struct sockaddr *)&client_addr, addr_len);
    }

    vTaskDelete(NULL);
}

esp_err_t dns_server_start(uint32_t resolved_ip)
{
    if (s_running) {
        return ESP_OK;
    }

    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0) {
        ESP_LOGE(TAG, "Failed to create DNS socket");
        return ESP_FAIL;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(s_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "Failed to bind DNS socket to port %d", DNS_PORT);
        close(s_sock);
        s_sock = -1;
        return ESP_FAIL;
    }

    s_resolved_ip = resolved_ip;
    s_running = true;
    xTaskCreate(dns_server_task, "dns_server", 4096, NULL, tskIDLE_PRIORITY + 1, &s_task);
    ESP_LOGI(TAG, "Captive portal DNS server started");
    return ESP_OK;
}

void dns_server_stop(void)
{
    if (!s_running) {
        return;
    }
    s_running = false;
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }
    /* dns_server_task exits its loop once recvfrom() returns after the
     * socket is closed, then deletes itself. */
}
