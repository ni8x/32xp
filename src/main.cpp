#include <Arduino.h>
#include "App.h"

App app;

void setup() {
    app.init();
}

void loop() {
    app.update();
}
