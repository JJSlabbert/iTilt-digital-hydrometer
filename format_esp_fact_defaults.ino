#include <FS.h>          // this needs to be first, or it all crashes and burns...
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager Install from Github
#include <ArduinoJson.h> // https://github.com/bblanchon/ArduinoJson Use version 5, Install from Arduino IDE

#ifdef ESP32
#include <SPIFFS.h>
#endif

WiFiManager wm;

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("Formatting the SPIFFS...");
  SPIFFS.format();
  Serial.println("Reset WiFiManager...");  
  wm.resetSettings();
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);

}

void loop() {
  // put your main code here, to run repeatedly:
  Serial.println("All should be restored to factory defaults by now");
  for (int i=0; i<10;i++)
  {  
    digitalWrite(LED_BUILTIN,HIGH);
    delay(1000);
    digitalWrite(LED_BUILTIN, LOW);
    delay(1000);
  }
}
