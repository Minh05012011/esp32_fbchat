#pragma once
#include <Arduino.h>

void sendMqttConnect();
void sendCreateQueue();
void processMqttBuffer();
void handleQueueError(const String& ecStr);
void handleMqttPublish(const String& topic, const String& body);
