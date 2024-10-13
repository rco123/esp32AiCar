#ifndef WS_SERVER_PC_H
#define WS_SERVER_PC_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_http_server.h>  // esp_http_server.h는 반드시 포함되어야 합니다.

#include <EEPROM.h>
#include "setMotor.h"

#define LED_BUILTIN 4

// 전역 변수 선언
int car_angle = 0;  //동작각도
int car_speed = 0;  // 동작스피드
int set_speed = 0;  // 셋팅엥글


// 대체 WebSocket 핸들러 함수 PC
esp_err_t alt_ws_handler(httpd_req_t *req) {
  if (req->method == HTTP_GET) {
    // WebSocket 핸들러는 WebSocket 연결을 자동으로 처리
    Serial.println("WebSocket connection opened");
    car_speed = 0; // 차동차 속도를 초기화 한다. 명령이 들어오면 재설정된다.
    return ESP_OK;
  }

  httpd_ws_frame_t ws_pkt;
  memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
  ws_pkt.type = HTTPD_WS_TYPE_TEXT;

  // 프레임 길이 수신
  esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
  if (ret != ESP_OK) {
    Serial.println("WebSocket connection closed by client or failed to receive frame.");
    return ret;
  }

  // 프레임 페이로드가 있는 경우
  if (ws_pkt.len > 0) {
    // 페이로드를 저장할 버퍼 할당
    uint8_t *buf = (uint8_t *)malloc(ws_pkt.len + 1);
    if (!buf) {
      Serial.println("Failed to allocate memory for WebSocket payload");
      return ESP_ERR_NO_MEM;
    }
    ws_pkt.payload = buf;

    // 실제 페이로드 수신
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
      Serial.printf("Failed to receive frame payload: %d\n", ret);
      free(buf);
      return ret;
    }

    buf[ws_pkt.len] = '\0';  // Null-terminate the payload
    //Serial.printf("Received WebSocket message: %s\n", buf);

    // 분할된 프레임 처리
    if (!ws_pkt.final) {
      Serial.println("Received fragmented frame, which is not supported.");
      free(buf);
      return ESP_FAIL;  // 또는 적절한 오류 코드
    }

    // JSON 데이터 처리
    StaticJsonDocument<256> jsonDoc;  // StaticJsonDocument을 올바르게 사용
    DeserializationError error = deserializeJson(jsonDoc, buf);
    if (error) {
      Serial.print("JSON parse error: ");
      Serial.println(error.c_str());
      free(buf);
      return ESP_FAIL;  // 적절한 오류 코드 반환
    }

    // do_alt PC 에서 사용하는 Commnad
    // 값 추출 및 기본값 설정 (const char* 사용)
    const char *cmd = jsonDoc["cmd"] | "";
    const char *state = jsonDoc["state"] | "";
    int angle = jsonDoc["angle"] | 0;
    int speed = jsonDoc["speed"] | 0;


    // 액션 처리
    if (strcmp(cmd, "move") == 0) {
      car_angle = angle;
      car_speed = speed;
      Serial.printf("car angle %d and speed %d updated\n", angle, speed);

      // 서보모터 제어나 다른 장치 제어 코드를 여기 추가할 수 있습니다.
      if (speed != 0) {
        int lspeed = car_speed + car_angle * 0.5;
        int rspeed = car_speed - car_angle * 0.5;
        Serial.printf("run Car forward, %d, %d", lspeed, rspeed);
        Car_forward(lspeed, rspeed);
      } else {
        Car_stop();
      }
    } else if (strcmp(cmd, "led") == 0) {  // 추가된 'led' 액션 처리
      if (strcmp(state, "on") == 0) {
        digitalWrite(LED_BUILTIN, HIGH);
        Serial.println("LED turned on");
      } else if (strcmp(state, "off") == 0) {
        digitalWrite(LED_BUILTIN, LOW);
        Serial.println("LED turned off");
      } else {
        Serial.println("Unknown state for LED ");
      }
    } else if (strcmp(cmd, "set_speed") == 0) {
      set_speed = speed;  // PC에서 요청하는 속도값을 전역변수에 저장한다.
      // EEPROM에 새로운 속도 값을 저장
      EEPROM.write(0, set_speed);  // EEPROM에 값 기록
      EEPROM.commit();             // 플래시 메모리에 기록 확정
      Serial.println("Speed value saved to EEPROM.");
    }

    else if (strcmp(cmd, "get_speed") == 0) {
      // 속도 값을 JSON으로 만들어서 클라이언트로 전송
      StaticJsonDocument<64> responseDoc;
      responseDoc["car_angle"] = car_angle;  // 전역 변수 set_speed에 저장된 값을 사용
      responseDoc["car_speed"] = car_speed;  // 전역 변수 set_speed에 저장된 값을 사용
      

      // JSON을 문자열로 변환
      char responseBuffer[64];
      size_t responseSize = serializeJson(responseDoc, responseBuffer);

      // WebSocket을 통해 클라이언트에 전송
      httpd_ws_frame_t ws_response;
      memset(&ws_response, 0, sizeof(httpd_ws_frame_t));
      ws_response.payload = (uint8_t *)responseBuffer;
      ws_response.len = responseSize;
      ws_response.type = HTTPD_WS_TYPE_TEXT;
      ret = httpd_ws_send_frame(req, &ws_response);
      if (ret != ESP_OK) {
        Serial.printf("Failed to send WebSocket response: %d\n", ret);
      } else {
        //Serial.println("Sent speed value via WebSocket.");
      }
    }

    // 클라이언트에 응답 보내기
    // httpd_ws_frame_t ws_res;
    // memset(&ws_res, 0, sizeof(httpd_ws_frame_t));
    // ws_res.type = HTTPD_WS_TYPE_TEXT;
    // const char *response = "Message received";
    // ws_res.payload = (uint8_t *)response;
    // ws_res.len = strlen(response);
    // ws_res.final = true;  // 최종 플래그 설정

    // ret = httpd_ws_send_frame(req, &ws_res);
    // if (ret != ESP_OK) {
    //   Serial.printf("Failed to send WebSocket frame: %d\n", ret);
    // }

    free(buf);
  } else {
    Serial.println("Received empty WebSocket message");
  }
  return ESP_OK;

}


#endif  // WS_SERVER_H
