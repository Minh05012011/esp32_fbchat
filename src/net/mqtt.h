#pragma once
#include <Arduino.h>

void sendMqttConnect();
void sendCreateQueue();
void sendMqttSubscribe(const String& topic, uint8_t qos = 0);
void processMqttBuffer();
void handleQueueError(const String& ecStr);
void handleMqttPublish(const String& topic, const String& body);