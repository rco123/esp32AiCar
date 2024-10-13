#ifndef APP_SERVER_H
#define APP_SERVER_H

// Define Servo PWM Values & Stopped Variable
int speed = 100;
int trim = 0;

// Libraries. If you get errors compiling, please downgrade ESP32 by Espressif.
// Use version 1.0.2 (Tools, Manage Libraries).
#include <esp32-hal-ledc.h>
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "Arduino.h"
#include "lib/dl_lib.h"
#include "SetMotor.h"
#include "jsonContwsPC.h"
#include "jsonContwsHP.h"


// 전역 변수 선언
volatile int client_count = 0;
SemaphoreHandle_t client_count_semaphore;
TaskHandle_t capture_task_handle = NULL;
volatile bool capture_task_running = false;


camera_fb_t *shared_fb = NULL;
SemaphoreHandle_t xSemaphore = NULL;  // 동기화를 위한 세마포어

static esp_err_t send_frame(httpd_req_t *req); // 프로토타입 선언
void capture_frame(void* param);

extern esp_err_t ws_handler(httpd_req_t *req);



#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t alt_stream_httpd = NULL;

httpd_handle_t ws_httpd = NULL;
httpd_handle_t alt_ws_httpd = NULL;


// stream_handler 수정
static esp_err_t stream_handler(httpd_req_t *req) {
    esp_err_t res = ESP_OK;

    Serial.printf("start stream_handler %d\n", client_count);

    // 클라이언트 수 증가
    if (xSemaphoreTake(client_count_semaphore, portMAX_DELAY)) {
        client_count++;
        if (client_count > 0) {
            // 첫 번째 클라이언트가 연결되었으므로 캡처 태스크 시작
            if (capture_task_handle == NULL) {
                capture_task_running = true;  // 플래그를 true로 설정
                xTaskCreate(capture_frame, "capture_frame", 4096, NULL, 1, &capture_task_handle);
            }
        }
        xSemaphoreGive(client_count_semaphore);
    }

    httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    while (true) {
        if (send_frame(req) != ESP_OK) {
            break;
        }
    }

    // 클라이언트 수 감소
    if (xSemaphoreTake(client_count_semaphore, portMAX_DELAY)) {
        client_count--;
        if (client_count == 0) {
            // 마지막 클라이언트가 연결을 해제했으므로 캡처 태스크 중지
            if (capture_task_handle != NULL) {
                capture_task_running = false;  // 플래그를 false로 설정
                xSemaphoreGive(client_count_semaphore);  // 세마포어 해제
                // 태스크가 종료될 시간을 주기 위해 지연
                vTaskDelay(pdMS_TO_TICKS(100));
                capture_task_handle = NULL;
            } else {
                xSemaphoreGive(client_count_semaphore);  // 세마포어 해제
            }
        } else {
            xSemaphoreGive(client_count_semaphore);  // 세마포어 해제
        }
    }

    Serial.printf("end stream_handler %d\n", client_count);

    return ESP_OK;
}


// stream_handler 수정
static esp_err_t alt_stream_handler(httpd_req_t *req) {
    esp_err_t res = ESP_OK;

    Serial.printf("start alt_stream_handler %d\n", client_count);

    // 클라이언트 수 증가
    if (xSemaphoreTake(client_count_semaphore, portMAX_DELAY)) {
        client_count++;
        if (client_count > 0) {
            // 첫 번째 클라이언트가 연결되었으므로 캡처 태스크 시작
            if (capture_task_handle == NULL) {
                capture_task_running = true;  // 플래그를 true로 설정
                xTaskCreate(capture_frame, "capture_frame", 4096, NULL, 1, &capture_task_handle);
            }
        }
        xSemaphoreGive(client_count_semaphore);
    }

    httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    while (true) {
        if (send_frame(req) != ESP_OK) {
            break;
        }
    }

    // 클라이언트 수 감소
    if (xSemaphoreTake(client_count_semaphore, portMAX_DELAY)) {
        client_count--;
        if (client_count == 0) {
            // 마지막 클라이언트가 연결을 해제했으므로 캡처 태스크 중지
            if (capture_task_handle != NULL) {
                capture_task_running = false;  // 플래그를 false로 설정
                xSemaphoreGive(client_count_semaphore);  // 세마포어 해제
                // 태스크가 종료될 시간을 주기 위해 지연
                vTaskDelay(pdMS_TO_TICKS(100));
                capture_task_handle = NULL;
            } else {
                xSemaphoreGive(client_count_semaphore);  // 세마포어 해제
            }
        } else {
            xSemaphoreGive(client_count_semaphore);  // 세마포어 해제
        }
    }

    Serial.printf("end alt_stream_handler %d\n", client_count);
    return ESP_OK;
}

static esp_err_t send_frame(httpd_req_t *req) {
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    char part_buf[64];

    // Take the semaphore to access shared_fb
    if (xSemaphoreTake(xSemaphore, portMAX_DELAY)) {
        if (shared_fb) {
            _jpg_buf_len = shared_fb->len;
            // Allocate memory for the frame copy
            _jpg_buf = (uint8_t *)malloc(_jpg_buf_len);
            if (_jpg_buf) {
                memcpy(_jpg_buf, shared_fb->buf, _jpg_buf_len);
            } else {
                Serial.println("Failed to allocate memory for frame copy");
                res = ESP_ERR_NO_MEM;
            }
        } else {
            // 프레임이 없는 경우 약간의 지연 후 다시 시도
            xSemaphoreGive(xSemaphore);
            vTaskDelay(pdMS_TO_TICKS(10));
            return ESP_OK;
        }
        // Release the semaphore
        xSemaphoreGive(xSemaphore);
    } else {
        Serial.println("Failed to take semaphore");
        res = ESP_FAIL;
    }

    if (res == ESP_OK && _jpg_buf) {
        // Send the frame to the client
        size_t hlen = snprintf(part_buf, sizeof(part_buf), _STREAM_PART, _jpg_buf_len);
        res = httpd_resp_send_chunk(req, part_buf, hlen);
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        // Free the copied frame buffer
        free(_jpg_buf);
    }

    return res;
}

void capture_frame(void* param) {
    while (capture_task_running) {
        if (xSemaphoreTake(xSemaphore, portMAX_DELAY)) {
            if (shared_fb) {
                esp_camera_fb_return(shared_fb);  // 이전 프레임 반환
            }
            shared_fb = esp_camera_fb_get();  // 새로운 프레임 가져오기
            if (!shared_fb) {
                Serial.println("Failed to capture frame");
            } else {
                //Serial.printf("Captured frame: %u bytes\n", shared_fb->len);
            }
            xSemaphoreGive(xSemaphore);
        }
        vTaskDelay(pdMS_TO_TICKS(10));  // 필요한 경우 지연 조정
    }

    // 태스크 종료 전 정리 작업
    if (xSemaphoreTake(xSemaphore, portMAX_DELAY)) {
        if (shared_fb) {
            esp_camera_fb_return(shared_fb);
            shared_fb = NULL;
        }
        xSemaphoreGive(xSemaphore);
    }

    Serial.println("Capture task stopping");
    vTaskDelete(NULL);  // 태스크 종료
}


// Finally, if all is well with the camera, encoding, and all else, here it is, the actual camera server.
// If it works, use your new camera robot to grab a beer from the fridge using function Request.Fridge("beer","buschlite")
void startCameraServer() {

  // 클라이언트 수 세마포어 초기화
  client_count_semaphore = xSemaphoreCreateMutex();
  // 세마포어 초기화
  xSemaphore = xSemaphoreCreateMutex();

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  httpd_uri_t stream_uri = {
    .uri = "/stream",
    .method = HTTP_GET,
    .handler = stream_handler,
    .user_ctx = NULL
  };

  httpd_uri_t alt_stream_uri = {
        .uri = "/alt_stream",
        .method = HTTP_GET,
        .handler = alt_stream_handler,
        .user_ctx = NULL
    };

  httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = NULL,
        .is_websocket = true
    };  

  httpd_uri_t alt_ws_uri = {
        .uri = "/alt_ws",
        .method = HTTP_GET,
        .handler = alt_ws_handler,
        .user_ctx = NULL,
        .is_websocket = true
    };  


  config.server_port =81;
  config.ctrl_port =1081;
  Serial.printf("Starting stream server on port: '%d'\n", config.server_port);
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }

  config.server_port =82;
  config.ctrl_port += 1082;
  Serial.printf("Starting stream server on port: '%d'\n", config.server_port);
  if (httpd_start(&alt_stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(alt_stream_httpd, &alt_stream_uri);  // 추가된 핸들러
  }

  // move control port

  config.server_port = 91;  // 포트 90번 설정
  config.ctrl_port = 1091;
  Serial.printf("Starting stream server on port: '%d'\n", config.server_port);
  if (httpd_start(&ws_httpd, &config) == ESP_OK) {
      // WebSocket 핸들러 등록
      if (httpd_register_uri_handler(ws_httpd, &ws_uri) == ESP_OK) {
          Serial.println("WebSocket server started on /ws");
      } else {
          Serial.println("Failed to register WebSocket handler");
      }
  }

  config.server_port = 92;  // 포트 91번 설정
  config.ctrl_port = 1092;
  Serial.printf("Starting stream server on port: '%d'\n", config.server_port);
  if (httpd_start(&alt_ws_httpd, &config) == ESP_OK) {
      // WebSocket 핸들러 등록
      if (httpd_register_uri_handler(alt_ws_httpd, &alt_ws_uri) == ESP_OK) {
          Serial.println("WebSocket server started on /alt_ws");
      } else {
          Serial.println("Failed to register WebSocket handler");
      }
  }


}

#endif
