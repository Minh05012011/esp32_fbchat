#pragma once
#include <Arduino.h>

void printSerialHelp();
void handleSerialCommand(String cmd);
void pollSerialInput();