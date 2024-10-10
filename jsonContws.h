#ifndef WS_SERVER_H
#define WS_SERVER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>


#define LED_BUILTIN 4
#include "esp_http_server.h"


esp_err_t ws_handler(httpd_req_t *req) {

    if (req->method == HTTP_GET) {
        // WebSocket handshake is done automatically when is_websocket is true
        Serial.println("WebSocket connection opened");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    // Get the frame length
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        Serial.printf("Failed to receive frame length: %d\n", ret);
        return ret;
    }

    // Allocate buffer for the frame payload
    if (ws_pkt.len > 0) {
        uint8_t *buf = (uint8_t *)malloc(ws_pkt.len + 1);
        if (!buf) {
            Serial.println("Failed to allocate memory for WebSocket payload");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;

        // Receive the actual frame payload
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            Serial.printf("Failed to receive frame payload: %d\n", ret);
            free(buf);
            return ret;
        }

        buf[ws_pkt.len] = '\0'; // Null-terminate the payload
        Serial.printf("Received WebSocket message: %s\n", buf);

        // Handle fragmented frames if necessary
        if (!ws_pkt.final) {
            Serial.println("Received fragmented frame, which is not supported.");
            free(buf);
            return ESP_FAIL; // Or handle accordingly
        }

        // Process JSON data
        StaticJsonDocument<256> jsonDoc;
        DeserializationError error = deserializeJson(jsonDoc, buf);
        if (error) {
            Serial.print("JSON parse error: ");
            Serial.println(error.c_str());
        } else {
            const char *action = jsonDoc["action"];
            const char *state = jsonDoc["state"];

            // Example handle action processing
            if (strcmp(action, "led") == 0) {
                if (strcmp(state, "on") == 0) {
                    digitalWrite(LED_BUILTIN, HIGH);
                    Serial.println("LED is ON");
                } else if (strcmp(state, "off") == 0) {
                    digitalWrite(LED_BUILTIN, LOW);
                    Serial.println("LED is OFF");
                }
            }
        }

        // Send response back to client
        httpd_ws_frame_t ws_res;
        ws_res.type = HTTPD_WS_TYPE_TEXT;
        ws_res.payload = (uint8_t *)"Message received";
        ws_res.len = strlen((char *)ws_res.payload);
        ws_res.final = true; // Ensure the final flag is set
        ret = httpd_ws_send_frame(req, &ws_res);
        if (ret != ESP_OK) {
            Serial.printf("Failed to send WebSocket frame: %d\n", ret);
        }

        free(buf);
    } else {
        Serial.println("Received empty WebSocket message");
    }

    return ESP_OK;
}


#endif  // WS_SERVER_H
