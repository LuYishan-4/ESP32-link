// lib/webservice/web_server.h
#pragma once

#include <stdint.h>
#include <stddef.h>

class AsyncWebServer;
class AsyncWebSocket;
class NetworkHandler;

class WebServerController {
public:
  void begin(NetworkHandler& net);

private:
  AsyncWebServer* _server = nullptr;
  AsyncWebSocket* _ws     = nullptr;
  NetworkHandler* _net    = nullptr;
};
