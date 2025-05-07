#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "lwip/tcp.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "hardware/adc.h"

#define BUTTON1_PIN 5
#define BUTTON2_PIN 6
#define LIMIAR_VARIACAO_TEMPERATURA 0.5f

#define WIFI_SSID "Arnaldojr"
#define WIFI_PASS "12345678"

char button1_message[50] = "Nenhum evento no botão 1";
char button2_message[50] = "Nenhum evento no botão 2";
char temperature_message[50] = "Temperatura: 0.00 °C";
char http_response[2024];

bool button1_pressed = false;
bool button2_pressed = false;


float ler_temperatura() {
    /* Conversão de 12-bit, valor máximo = ADC_VREF = 3.3V */
    const float fator_conversao = 3.3f / (1 << 12);

    float adc = (float)adc_read() * fator_conversao;
    float temperatura = 27.0f - (adc - 0.706f) / 0.001721f;
    
    return temperatura;
}


void create_http_response() {
    snprintf(http_response, sizeof(http_response),
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n"
        "<!DOCTYPE html>"
        "<html lang=\"pt\">"
        "<head>"
        "  <meta http-equiv=\"refresh\" content=\"1\">" 
        "  <meta charset=\"UTF-8\">"
        "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
        "  <title>Pico W - Controle de LED</title>"
        "  <style>"
        "    body { font-family: Arial, sans-serif; text-align: center; padding: 20px; background-color: #f0f0f0; }"
        "    h1 { color: #333; }"
        "    .button { display: inline-block; padding: 10px 20px; margin: 10px; font-size: 16px; color: white; background-color: #007BFF; border: none; border-radius: 5px; text-decoration: none; }"
        "    .button:hover { background-color: #0056b3; }"
        "    .status { margin-top: 20px; font-size: 18px; }"
        "    .on { color: green; font-weight: bold; }"
        "    .off { color: red; font-weight: bold; }"
        "  </style>"
        "  <script>"
        "    setTimeout(() => { location.reload(); }, 1000);"
        "  </script>"
        "</head>"
        "<body>"
        "  <h1>Interface WebServer - Pico W</h1>"
        "  <a href=\"/led/on\" class=\"button\">Ligar LED</a>"
        "  <a href=\"/led/off\" class=\"button\">Desligar LED</a>"
        "  <div class=\"status\">"
        "    <h2>Estado dos Botões:</h2>"
        "    <p>Botão 1: <span class=\"%s\">%s</span></p>"
        "    <p>Botão 2: <span class=\"%s\">%s</span></p>"
        "    <h2>Temperatura Atual:</h2>"
        "    <p>%s</p>"
        "  </div>"
        "</body>"
        "</html>\r\n",
        button1_pressed ? "on" : "off", button1_message,
        button2_pressed ? "on" : "off", button2_message,
        temperature_message);
}

static err_t http_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (p == NULL) {
        tcp_close(tpcb);
        return ERR_OK;
    }

    char *request = (char *)p->payload;

    if (strstr(request, "GET /led/on")) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        printf("LED ligado\n");
    } else if (strstr(request, "GET /led/off")) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        printf("LED desligado\n");
    }

    create_http_response();
    tcp_write(tpcb, http_response, strlen(http_response), TCP_WRITE_FLAG_COPY);
    pbuf_free(p);
    return ERR_OK;
}

static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err) {
    tcp_recv(newpcb, http_callback);
    return ERR_OK;
}

static void start_http_server(void) {
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb) {
        printf("Erro ao criar PCB\n");
        return;
    }

    if (tcp_bind(pcb, IP_ADDR_ANY, 80) != ERR_OK) {
        printf("Erro ao ligar na porta 80\n");
        return;
    }

    pcb = tcp_listen(pcb);
    tcp_accept(pcb, connection_callback);

    printf("Servidor HTTP iniciado na porta 80...\n");
}

int main() {
    stdio_init_all();

    adc_init();
    adc_set_temp_sensor_enabled(true);
    adc_select_input(4);

    printf("Iniciando servidor HTTP\n");

    if (cyw43_arch_init()) {
        printf("Erro ao inicializar Wi-Fi\n");
        return 1;
    }

    cyw43_arch_enable_sta_mode();
    printf("Conectando ao Wi-Fi...\n");

    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK, 10000)) {
        printf("Falha ao conectar ao Wi-Fi\n");
        return 1;
    } else {
        uint8_t *ip_address = (uint8_t*)&(cyw43_state.netif[0].ip_addr.addr);
        printf("Conectado com IP: %d.%d.%d.%d\n", ip_address[0], ip_address[1], ip_address[2], ip_address[3]);
    }

    gpio_init(BUTTON1_PIN);
    gpio_set_dir(BUTTON1_PIN, GPIO_IN);
    gpio_pull_up(BUTTON1_PIN);

    gpio_init(BUTTON2_PIN);
    gpio_set_dir(BUTTON2_PIN, GPIO_IN);
    gpio_pull_up(BUTTON2_PIN);

    start_http_server();

    static bool button1_last_state = false;
    static bool button2_last_state = false;
    static float temperatura_anterior = 0;

    while (true) {
        cyw43_arch_poll();

        bool button1_state = !gpio_get(BUTTON1_PIN);
        bool button2_state = !gpio_get(BUTTON2_PIN);
        float temperatura = ler_temperatura();

        if (button1_state != button1_last_state) {
            button1_last_state = button1_state;
            button1_pressed = button1_state;
            if (button1_state) {
                snprintf(button1_message, sizeof(button1_message), "Botão 1 foi pressionado!");
            } else {
                snprintf(button1_message, sizeof(button1_message), "Botão 1 foi solto!");
            }
            printf("%s\n", button1_message);
        }

        if (button2_state != button2_last_state) {
            button2_last_state = button2_state;
            button2_pressed = button2_state;
            if (button2_state) {
                snprintf(button2_message, sizeof(button2_message), "Botão 2 foi pressionado!");
            } else {
                snprintf(button2_message, sizeof(button2_message), "Botão 2 foi solto!");
            }
            printf("%s\n", button2_message);
        }

        if (temperatura - temperatura_anterior >= LIMIAR_VARIACAO_TEMPERATURA) {
            temperatura_anterior = temperatura;
            snprintf(temperature_message, sizeof(temperature_message), "Temperatura: %.2f°C", temperatura);
            printf("Temperatura: %s °C\n", temperatura);
        }


        sleep_ms(100);
    }

    cyw43_arch_deinit();
    return 0;
}
