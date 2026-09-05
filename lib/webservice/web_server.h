// lib/webservice/web_server.h
#pragma once

#include <stdint.h>
#include <stddef.h>

class AsyncWebServer;
class AsyncWebSocket;
class NetworkManager;

class WebServerController {
public:
  void begin(NetworkManager& net);

private:
  AsyncWebServer* _server = nullptr;
  AsyncWebSocket* _ws     = nullptr;
  NetworkManager* _net    = nullptr;
};
