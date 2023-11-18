//incluimos las librerias necesarias
#include <stdio.h>
#include <string.h>
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "esp_http_server.h"
#include "driver/ledc.h"
#include "esp_log.h"

//defines
#define LEDR_PIN      17
#define LEDG_PIN      18
#define LEDB_PIN      5

//prototipos de funciones
static void esp_logs(esp_err_t* err, char* msg);
static void web_server_init(void);
static esp_err_t api_get_handler(httpd_req_t* req);
static esp_err_t home_get_handler(httpd_req_t* req);
static esp_err_t chroma_get_handler(httpd_req_t* req);

//declaramos las variables globales
extern const char index_start[] asm("_binary_index_html_start");
extern const char index_end[] asm("_binary_index_html_end");
extern const char chroma_start[] asm("_binary_chroma_png_start");
extern const char chroma_end[] asm("_binary_chroma_png_end");
static const char* TAG = "ESP32";
static const httpd_uri_t api = {
   .uri = "/api",
   .method = HTTP_GET,
   .handler = api_get_handler
};
static const httpd_uri_t home = {
   .uri = "/",
   .method = HTTP_GET,
   .handler = home_get_handler
};
static const httpd_uri_t chroma = {
   .uri = "/chroma.png",
   .method = HTTP_GET,
   .handler = chroma_get_handler
};
ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK
};
ledc_channel_config_t ledr_chnl = {
    .speed_mode = LEDC_HIGH_SPEED_MODE,
    .channel = LEDC_CHANNEL_0,
    .timer_sel = LEDC_TIMER_0,
    .intr_type = LEDC_INTR_DISABLE, //interrupciones deshabilitadas
    .gpio_num = LEDR_PIN,   //pin de salida PWM
    .duty = 255,      //ciclo util inicial
    .hpoint = 0     //ajuste de fase
};
ledc_channel_config_t ledg_chnl = {
    .speed_mode = LEDC_HIGH_SPEED_MODE,
    .channel = LEDC_CHANNEL_1,
    .timer_sel = LEDC_TIMER_0,
    .intr_type = LEDC_INTR_DISABLE, //interrupciones deshabilitadas
    .gpio_num = LEDG_PIN,   //pin de salida PWM
    .duty = 255,      //ciclo util inicial
    .hpoint = 0     //ajuste de fase
};
ledc_channel_config_t ledb_chnl = {
    .speed_mode = LEDC_HIGH_SPEED_MODE,
    .channel = LEDC_CHANNEL_2,
    .timer_sel = LEDC_TIMER_0,
    .intr_type = LEDC_INTR_DISABLE, //interrupciones deshabilitadas
    .gpio_num = LEDB_PIN,   //pin de salida PWM
    .duty = 255,      //ciclo util inicial
    .hpoint = 0     //ajuste de fase
};
int32_t r = 0, g = 0, b = 0;
uint32_t duty_red = 255;
uint32_t duty_green = 255;
uint32_t duty_blue = 255;
esp_err_t err;

void app_main(void) {
   ESP_LOGI(TAG, "Start Application!");

   //inicializamos el driver de NVS
   err = nvs_flash_init();
   esp_logs(&err, "NVS driver initialization");

   //configuramos el timer para el PWM del LED
   err = ledc_timer_config(&ledc_timer);
   esp_logs(&err, "LEDC Timer config");
   //configuramos el canal PWM para el led rojo, verde y azul
   err = ledc_channel_config(&ledr_chnl);
   esp_logs(&err, "LED Red PWM Channel config");
   err = ledc_channel_config(&ledg_chnl);
   esp_logs(&err, "LED Green PWM Channel config");
   err = ledc_channel_config(&ledb_chnl);
   esp_logs(&err, "LED Blue PWM Channel config");

   esp_netif_init();
   esp_event_loop_create_default();
   example_connect();

   esp_netif_ip_info_t ip_info;
   esp_netif_t* netif = NULL;
   netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

   if (netif != NULL) {
      esp_netif_get_ip_info(netif, &ip_info);
      ESP_LOGI(TAG, "IP info: %d.%d.%d.%d", IP2STR(&ip_info.ip));

      //inicializamos los 3 canales a 255 para apagar los 3 colores del LED RGB
      ledc_set_duty(ledr_chnl.speed_mode, ledr_chnl.channel, duty_red);
      ledc_update_duty(ledr_chnl.speed_mode, ledr_chnl.channel);
      ledc_set_duty(ledg_chnl.speed_mode, ledg_chnl.channel, duty_green);
      ledc_update_duty(ledg_chnl.speed_mode, ledg_chnl.channel);
      ledc_set_duty(ledb_chnl.speed_mode, ledb_chnl.channel, duty_blue);
      ledc_update_duty(ledb_chnl.speed_mode, ledb_chnl.channel);

      web_server_init();
   }
   else {
      ESP_LOGE(TAG, "No exist WiFi interface");
   }
}
//-------------------------

static void esp_logs(esp_err_t* err, char* msg) {
   if (*err == ESP_OK) ESP_LOGI(TAG, "%s done.", msg);
   else ESP_LOGE(TAG, "%s fail!!", msg);
}
//-------------------------

static void web_server_init(void) {
   httpd_handle_t server = NULL;
   httpd_config_t config = HTTPD_DEFAULT_CONFIG();

   //inicializamos el servidor
   err = httpd_start(&server, &config);
   esp_logs(&err, "HTTP Server start");

   if (err == ESP_OK) {
      httpd_register_uri_handler(server, &api);
      httpd_register_uri_handler(server, &home);
      httpd_register_uri_handler(server, &chroma);
   }
}
//-------------------------

static esp_err_t api_get_handler(httpd_req_t* req) {
   //envio de la solicitud desde la url ip/api?r=0&g=0&b=0
   char* buf;
   size_t buf_len;

   buf_len = httpd_req_get_url_query_len(req) + 1;
   if (buf_len > 1) {
      //reservamos un espacio en memoria para el buffer
      buf = malloc(buf_len);
      //almacenamos en buf todo el contenido de la query
      err = httpd_req_get_url_query_str(req, buf, buf_len);
      esp_logs(&err, "get query");

      if (err == ESP_OK) {
         //obtenemos los datos de RGB
         char param[4];
         if (httpd_query_key_value(buf, "r", param, 4) == ESP_OK) {
            r = atoi(param);
            printf("led red value: %ld\n", r);
         }
         if (httpd_query_key_value(buf, "g", param, 4) == ESP_OK) {
            g = atoi(param);
            printf("led green value: %ld\n", g);
         }
         if (httpd_query_key_value(buf, "b", param, 4) == ESP_OK) {
            b = atoi(param);
            printf("led blue value: %ld\n", b);
         }
      }
      free(buf);  //liberamos la memoria 
   }

   //actualizamos el valor PWM del led RGB de anodo comun
   duty_red = 255 - r;
   duty_green = 255 - g;
   duty_blue = 255 - b;
   ledc_set_duty(ledr_chnl.speed_mode, ledr_chnl.channel, duty_red);
   ledc_update_duty(ledr_chnl.speed_mode, ledr_chnl.channel);
   ledc_set_duty(ledg_chnl.speed_mode, ledg_chnl.channel, duty_green);
   ledc_update_duty(ledg_chnl.speed_mode, ledg_chnl.channel);
   ledc_set_duty(ledb_chnl.speed_mode, ledb_chnl.channel, duty_blue);
   ledc_update_duty(ledb_chnl.speed_mode, ledb_chnl.channel);

   //header para que lo interprete el navegador
   httpd_resp_set_hdr(req, "Content-Type", "application/json");

   char resp[100] = "";
   sprintf(resp, "{ \"r\": %lu, \"g\": %lu, \"b\": %lu }", r, g, b);

   httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

   return ESP_OK;
}
//-------------------------

//devuelve el archivo index.html
static esp_err_t home_get_handler(httpd_req_t* req) {
   httpd_resp_set_type(req, "text/html");

   //devolvemos el archivo html
   const uint32_t index_len = index_end - index_start;
   httpd_resp_send(req, index_start, index_len);

   return ESP_OK;
}
//-------------------------

//devuelve el archivo chroma.png
static esp_err_t chroma_get_handler(httpd_req_t* req) {
   httpd_resp_set_type(req, "image/png");

   //devolvemos la imagen png
   const uint32_t chroma_len = chroma_end - chroma_start;
   httpd_resp_send(req, chroma_start, chroma_len);
   return ESP_OK;
}
//-------------------------
