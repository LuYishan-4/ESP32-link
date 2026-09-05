// lib/webservice/api_routes.h
#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

class AsyncWebServer;
class AsyncWebSocket;
class NetworkManager;

void registerApiRoutes(AsyncWebServer& server, AsyncWebSocket& ws, NetworkManager& net);
String apiStatusJson(NetworkManager& net);
String telemetryToJson(const uint8_t* packet, size_t len);
