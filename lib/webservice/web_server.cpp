// lib/webservice/web_server.cpp
#include "web_server.h"

#include <ESPAsyncWebServer.h>

#include "api_routes.h"
#include "html_pages.h"
#include "network/network_manager.h"

void WebServerController::begin(NetworkManager& net) {
  _net    = &net;
  _server = new AsyncWebServer(80);
  _ws     = new AsyncWebSocket("/ws");

  _ws->onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client,
                      AwsEventType type, void* arg, uint8_t* data, size_t len) {
    (void)server; (void)arg; (void)data; (void)len;
    if (type == WS_EVT_CONNECT && _net) {
      String s = apiStatusJson(*_net);
      client->text(s.c_str());
    }
  });
  _server->addHandler(_ws);

  registerApiRoutes(*_server, *_ws, *_net);

  _server->on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", HTML_PAGE);
  });

  _server->begin();

  // Live telemetry push over the WebSocket.
  net.setDataCallback([this](const uint8_t* packet, size_t len) {
    if (!_ws) return;
    String s = telemetryToJson(packet, len);
    if (s.length()) _ws->textAll(s.c_str());
  });
}
