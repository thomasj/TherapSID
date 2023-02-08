#pragma once

#include <Arduino.h>

void showVersion();
void leds();
void ledSet(byte number, bool value);
void rightDot();
void leftDot();
void ledNumber(int number);
void digit(byte channel, byte number);
