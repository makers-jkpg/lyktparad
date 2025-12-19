#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#define LED_R    0
#define LED_G    1
#define LED_B    2

#define LED_PIN    10      // WS2812 data-in pin on ESP32-C3-Zero
#define LED_COUNT  1       // just the single onboard RGB LED

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

int i;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32-C3 is alive and running setup()");
  i = 0;

  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);

  strip.begin();
  strip.show();  // Initialize all pixels to 'off'
  
  // Set the LED to bright green (R,G,B)
  strip.setPixelColor(0, strip.Color(0, 150, 0));
  strip.show();

  digitalWrite(LED_R, LOW);
  digitalWrite(LED_G, LOW);
  digitalWrite(LED_B, LOW);
}

void loop() {
  i++;
//  Serial.println("loop() tick");

  digitalWrite(LED_R, HIGH);
  digitalWrite(LED_G, LOW);
  digitalWrite(LED_B, LOW);
  delay(300);
  digitalWrite(LED_R, LOW);
  digitalWrite(LED_G, HIGH);
  digitalWrite(LED_B, LOW);
  delay(300);
  digitalWrite(LED_R, LOW);
  digitalWrite(LED_G, LOW);
  digitalWrite(LED_B, HIGH);
  delay(300);
  digitalWrite(LED_R, HIGH);
  digitalWrite(LED_G, HIGH);
  digitalWrite(LED_B, HIGH);
  delay(300);

  strip.setPixelColor(0, strip.Color(0, 0, (i*10)%100));
  strip.show();

}
