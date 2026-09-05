// lib/webservice/api_routes.h
#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

class AsyncWebServer;
class AsyncWebSocket;
class NetworkHandler;

void registerApiRoutes(AsyncWebServer& server, AsyncWebSocket& ws, NetworkHandler& net);
String apiStatusJson(NetworkHandler& net);
String telemetryToJson(const uint8_t* packet, size_t len);
