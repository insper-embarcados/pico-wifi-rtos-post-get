#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "lwip/err.h"
#include "lwip/netdb.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

// Configurações de Wi-Fi e servidor
#define WIFI_SSID "FERNANDES2"
#define WIFI_PASSWORD "17082001"
#define SERVER_DOMAIN "api.openweathermap.org"
#define SERVER_PORT 80

// Tamanho máximo do buffer de recepção
#define RECV_BUFFER_SIZE 2048

// Estrutura para manter o estado do cliente TCP
typedef struct {
    struct tcp_pcb *pcb;
    ip_addr_t server_ip;
    char recv_buffer[RECV_BUFFER_SIZE];
    int recv_len;
    SemaphoreHandle_t recv_sem;
    SemaphoreHandle_t dns_sem; // Semáforo para DNS
} tcp_client_t;

// Função para inicializar a conexão Wi-Fi
void wifi_init(void) {
    stdio_init_all();

    if (cyw43_arch_init()) {
        printf("Falha na inicialização do Wi-Fi\n");
        while (1) tight_loop_contents();
    }
    printf("Wi-Fi inicializado com sucesso\n");

    cyw43_arch_enable_sta_mode();

    if (cyw43_arch_wifi_connect_blocking(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK)) {
        printf("Falha ao conectar ao Wi-Fi\n");
        while (1) tight_loop_contents();
    }
    printf("Conectado ao Wi-Fi\n");
}

// Callback para quando a resolução DNS for concluída
static void my_dns_found_callback(const char *name, const ip_addr_t *ipaddr, void *callback_arg) {
    tcp_client_t *client = (tcp_client_t *)callback_arg;
    if (ipaddr == NULL) {
        printf("Falha ao resolver o domínio: %s\n", name);
    } else {
        printf("Domínio %s resolvido para IP: %s\n", name, ipaddr_ntoa(ipaddr));
        client->server_ip = *ipaddr;
    }
    xSemaphoreGive(client->dns_sem); // Libera o semáforo DNS
}

// Callback para quando a conexão TCP for estabelecida
static err_t tcp_client_connected(void *arg, struct tcp_pcb *tpcb, err_t err) {
    tcp_client_t *client = (tcp_client_t *)arg;
    if (err != ERR_OK) {
        printf("Falha na conexão TCP: %d\n", err);
    } else {
        printf("Conexão TCP estabelecida\n");
    }
    xSemaphoreGive(client->recv_sem); // Libera o semáforo em qualquer caso
    return err;
}

// Callback para quando dados forem recebidos
static err_t tcp_client_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    tcp_client_t *client = (tcp_client_t *)arg;

    if (!p) {
        // Conexão fechada pelo servidor
        printf("Conexão TCP fechada pelo servidor\n");
        xSemaphoreGive(client->recv_sem); // Libera o semáforo ao finalizar recepção
        return ERR_OK;
    }

    if (p->tot_len > 0) {
        // Limita o tamanho dos dados recebidos ao tamanho do buffer
        int len = (p->tot_len > RECV_BUFFER_SIZE - client->recv_len) ? RECV_BUFFER_SIZE - client->recv_len : p->tot_len;
        pbuf_copy_partial(p, client->recv_buffer + client->recv_len, len, 0);
        client->recv_len += len;
        client->recv_buffer[client->recv_len] = '\0'; // Garante terminação da string

        tcp_recved(tpcb, p->tot_len); // Informa ao TCP quanto de dados foram recebidos
    }

    pbuf_free(p); // Libera o buffer

    return ERR_OK;
}

// Callback para erros na conexão TCP
static void tcp_client_error(void *arg, err_t err) {
    tcp_client_t *client = (tcp_client_t *)arg;
    printf("Erro na conexão TCP: %d\n", err);
    xSemaphoreGive(client->recv_sem); // Libera o semáforo em caso de erro
}

// Função para enviar requisições HTTP
void send_http_request(const char *request) {
    tcp_client_t client;
    memset(&client, 0, sizeof(tcp_client_t));
    client.recv_sem = xSemaphoreCreateBinary();
    client.dns_sem = xSemaphoreCreateBinary();

    // Inicia a resolução DNS
    err_t err = dns_gethostbyname(SERVER_DOMAIN, &client.server_ip, my_dns_found_callback, &client);
    if (err == ERR_INPROGRESS) {
        // A resolução está em andamento, aguarda o semáforo
        if (xSemaphoreTake(client.dns_sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
            printf("Timeout na resolução DNS\n");
            vSemaphoreDelete(client.recv_sem);
            vSemaphoreDelete(client.dns_sem);
            return;
        }
    } else if (err == ERR_OK) {
        // O endereço IP já foi resolvido (está em cache)
        printf("Domínio %s resolvido para IP: %s (cache)\n", SERVER_DOMAIN, ipaddr_ntoa(&client.server_ip));
    } else {
        printf("Erro na resolução DNS: %d\n", err);
        vSemaphoreDelete(client.recv_sem);
        vSemaphoreDelete(client.dns_sem);
        return;
    }

    // Cria um novo PCB TCP
    client.pcb = tcp_new();
    if (!client.pcb) {
        printf("Falha ao criar PCB TCP\n");
        vSemaphoreDelete(client.recv_sem);
        vSemaphoreDelete(client.dns_sem);
        return;
    }

    // Conecta ao servidor
    tcp_arg(client.pcb, &client);
    tcp_err(client.pcb, tcp_client_error);
    tcp_recv(client.pcb, tcp_client_recv);

    err = tcp_connect(client.pcb, &client.server_ip, SERVER_PORT, tcp_client_connected);
    if (err != ERR_OK) {
        printf("Falha ao iniciar conexão TCP: %d\n", err);
        tcp_abort(client.pcb);
        vSemaphoreDelete(client.recv_sem);
        vSemaphoreDelete(client.dns_sem);
        return;
    }

    // Aguarda a conexão ser estabelecida ou ocorrer um erro
    if (xSemaphoreTake(client.recv_sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
        printf("Timeout ao conectar ao servidor\n");
        tcp_abort(client.pcb);
        vSemaphoreDelete(client.recv_sem);
        vSemaphoreDelete(client.dns_sem);
        return;
    }

    // Envia a requisição HTTP
    err = tcp_write(client.pcb, request, strlen(request), TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        printf("Falha ao enviar dados: %d\n", err);
        tcp_abort(client.pcb);
        vSemaphoreDelete(client.recv_sem);
        vSemaphoreDelete(client.dns_sem);
        return;
    }
    tcp_output(client.pcb); // Garante que os dados sejam enviados

    // Aguarda a resposta do servidor
    if (xSemaphoreTake(client.recv_sem, pdMS_TO_TICKS(5000)) == pdTRUE) {
        printf("Resposta do servidor:\n%s\n", client.recv_buffer);
    } else {
        printf("Timeout ao receber resposta do servidor\n");
    }

    // Fecha a conexão TCP
    tcp_close(client.pcb);
    vSemaphoreDelete(client.recv_sem);
    vSemaphoreDelete(client.dns_sem);
}

// Tarefa principal para enviar requisições HTTP
void http_client_task(void *pvParameters) {
    int contador = 0;

    while (1) {
        // Requisição HTTP POST
        char post_payload[128];
        snprintf(post_payload, sizeof(post_payload), "dado=%d", contador);

        char post_request[512];
        snprintf(post_request, sizeof(post_request),
                 "POST /post_data HTTP/1.1\r\n"
                 "Host: %s\r\n"
                 "Content-Type: application/x-www-form-urlencoded\r\n"
                 "Content-Length: %d\r\n"
                 "Connection: close\r\n"
                 "\r\n"
                 "%s",
                 SERVER_DOMAIN, strlen(post_payload), post_payload);

        printf("Enviando requisição HTTP POST...\n");
        send_http_request(post_request);
        contador++;

        vTaskDelay(pdMS_TO_TICKS(2000));

        // Requisição HTTP GET
        // char get_request[256];
        // snprintf(get_request, sizeof(get_request),
        //          "GET /get_counter HTTP/1.1\r\n"
        //          "Host: %s\r\n"
        //          "Connection: close\r\n"
        //          "\r\n",
        //          SERVER_DOMAIN);
        char get_request[512];
        snprintf(get_request, sizeof(get_request),
                "GET /data/2.5/weather?q=cotia&appid=ae10626011dbb050cfebc1ffa52f7829&units=metric HTTP/1.1\r\n"
                "Host: %s\r\n"
                "Connection: close\r\n"
                "\r\n",
                SERVER_DOMAIN);

        printf("Enviando requisição HTTP GET...\n");
        send_http_request(get_request);

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

int main() {
    wifi_init();

    // Cria a tarefa para enviar requisições HTTP
    xTaskCreate(http_client_task, "HTTP Client Task", 4096, NULL, 1, NULL);

    // Inicia o agendador do FreeRTOS
    vTaskStartScheduler();

    // Loop infinito (nunca alcançado)
    while (1) tight_loop_contents();
}
