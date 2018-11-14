#define IMU

#include <Arduino.h>
#include <MiniGrafx.h>
#include <EPD_WaveShare_75.h>
#include "image.h"
#include <EEPROM.h>
#include "ADXL345.h"

#define EPD_CS      2
#define EPD_RST     15
#define EPD_DC      5
#define EPD_BUSY    4
#define IMU_SDA     21
#define IMU_SCL     22
#define WAKE_UP_PIN GPIO_NUM_27
#define LED_PIN     26

#define MINI_BLACK 0
#define MINI_WHITE 1

#define EEPROM_SIZE 64

#define UPDATE_INTERVAL_SECS 5 * 60

uint16_t palette[] = {MINI_BLACK, MINI_WHITE};

#define BITS_PER_PIXEL 1
#define USE_SERIAL Serial

EPD_WaveShare75 epd(EPD_CS, EPD_RST, EPD_DC, EPD_BUSY);
MiniGrafx gfx = MiniGrafx(&epd, BITS_PER_PIXEL, palette);


ADXL345 accelerometer;

uint8_t lastRotation = 0;
hw_timer_t *timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
volatile byte ledState = false;
volatile long timerCalls = 30;
volatile long lastBlinkCall = 0;


void IRAM_ATTR blink() {
  timerCalls++;
  if (ledState || (timerCalls - lastBlinkCall > 25)) {
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState);
    lastBlinkCall = timerCalls;
  }
}

void setupIMU() {
#ifdef IMU
  if (!accelerometer.begin(IMU_SDA, IMU_SCL))
  {
    Serial.println("Could not find a valid ADXL345 sensor, check wiring!");
    delay(500);
  }


  // Set tap detection on Z-Axis
  accelerometer.setTapDetectionX(0);       // Don't check tap on X-Axis
  accelerometer.setTapDetectionY(0);       // Don't check tap on Y-Axis
  accelerometer.setTapDetectionZ(1);       // Check tap on Z-Axis
  // or
  // accelerometer.setTapDetectionXYZ(1);  // Check tap on X,Y,Z-Axis

  accelerometer.setTapThreshold(2.5);      // Recommended 2.5 g
  accelerometer.setTapDuration(0.02);      // Recommended 0.02 s
  accelerometer.setDoubleTapLatency(0.10); // Recommended 0.10 s
  accelerometer.setDoubleTapWindow(0.30);  // Recommended 0.30 s

  accelerometer.setActivityThreshold(2.0);    // Recommended 2 g
  accelerometer.setInactivityThreshold(2.0);  // Recommended 2 g
  accelerometer.setTimeInactivity(5);         // Recommended 5 s

  // Set activity detection only on X,Y,Z-Axis
  //accelerometer.setActivityXYZ(1);         // Check activity on X,Y,Z-Axis
  // or
  accelerometer.setActivityX(1);        // Check activity on X_Axis
  accelerometer.setActivityY(1);        // Check activity on Y-Axis
  accelerometer.setActivityZ(0);        // Check activity on Z-Axis

  // Set inactivity detection only on X,Y,Z-Axis
  //accelerometer.setInactivityXYZ(1);       // Check inactivity on X,Y,Z-Axis

  // Select INT 1 for get activities
  accelerometer.useInterrupt(ADXL345_INT1);
  pinMode(WAKE_UP_PIN, INPUT);
#endif

}

uint8_t getRotation(uint8_t sampleCount) {
#ifdef IMU
  Vector norm = accelerometer.readNormalize();
  // We need to read activities to flush FIFO buffer
  Activites activites = accelerometer.readActivites();
  uint8_t rotation = 0;
  uint8_t unchangedRotationCount = 0;
  uint8_t currentRotation = 0;
  while (true) {
    if (norm.ZAxis > 8) {
      return 4;
    } else if (norm.XAxis > 8) {
      currentRotation = 1;
    } else if (norm.XAxis < -8) {
      currentRotation = 3;
    } else if (norm.YAxis > 8) {
      currentRotation = 2;
    } else if (norm.YAxis < -8) {
      currentRotation = 0;
    } else {
      currentRotation =  3;
    }
    if (rotation == currentRotation) {
      unchangedRotationCount++;
    } else {
      rotation = currentRotation;
      unchangedRotationCount = 0;
    }

    if (unchangedRotationCount > sampleCount) {
      return rotation;
    }
    delay (100);
  }
#endif
  return 0;
}

void sleep() {
  epd.Sleep();
  Serial.printf("\n\n***Time before going to sleep %ld\n", millis());
  ESP.deepSleep(UPDATE_INTERVAL_SECS * 1000000);
}

void activity() {
  Serial.println("Interrupt triggered");
}

void setup() {
    pinMode(LED_PIN, OUTPUT);
    timer = timerBegin(0, 80, true);
    timerAttachInterrupt(timer, &blink, true);
    timerAlarmWrite(timer, 100000, true);
    timerAlarmEnable(timer);
    USE_SERIAL.begin(115200);
    
    if (!EEPROM.begin(EEPROM_SIZE)) {
      Serial.println("failed to initialise EEPROM");
    }

    setupIMU();


    gfx.init();




}

void loop() {
  uint8_t rotation = getRotation(5);

  // uint8_t lastRotation = EEPROM.read(0);
  String text = "";
  int epdRotation = rotation;
  switch(rotation) {
    case 0: 
      text = "Landscape, Up";
      break;
    case 1:
      text = "Portrait, Up";
      break;
    case 2:
      text = "Landscape, Down";
      break;
    case 3:
      text = "Portrait, Down";
      break;
    case 4:
      text = "Flat";
      epdRotation = 0;
      break;  
  }

  gfx.setRotation(epdRotation);
  gfx.fillBuffer(1);
  gfx.setColor(0);
  gfx.setFont(ArialMT_Plain_24);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.drawString(gfx.getWidth() / 2, gfx.getHeight() / 2 - 24, text);
  gfx.commit();

  //EEPROM.write(0, rotation);
  // EEPROM.commit();
  lastRotation = rotation;
  rotation = getRotation(5);
  if (rotation == lastRotation) {
    esp_sleep_enable_ext0_wakeup(WAKE_UP_PIN, 1);
    sleep();
  }

}
