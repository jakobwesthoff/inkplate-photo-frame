#ifndef TINYPICO_WAVESHARE_EPD
#include "Inkplate.h"
#else
#include <SPI.h>
#include "Adafruit_ACEP_PSRAM.h"
#include <TinyPICO.h>
#endif

#include "SdFat.h"
#include "EEPROM.h"
#include "driver/rtc_io.h"

// Uncomment this line, if you have one of the newer inkplate 10s, which have a
// different (darker) color spectrum.
//#define USE_INKPLATE_LIGHTMODE

// #define ALWAYS_SHOW_BATTERY
#define BATTERY_WARNING_LEVEL 3.7

// #define uS_TO_SLEEP 10800000000 // 3h
//#define uS_TO_SLEEP 5400000000 //1.5h
//#define uS_TO_SLEEP 2700000000 //45m
// #define uS_TO_SLEEP 10000000 // 5s
#define uS_TO_SLEEP 3600000000 // 1h

#ifdef TINYPICO_WAVESHARE_EPD
#define EPD_CS 14
#define EPD_DC 4
#define EPD_RESET 15
#define EPD_BUSY 27

#define SD_MISO 22
#define SD_MOSI 21
#define SD_CLK 32
#define SD_CS 5

#define APA_102_PWR 13

#define E_INK_WIDTH 600
#define E_INK_HEIGHT 448
#endif

#define MAX_PHOTOS 2048
#define EEPROM_MAGIC 0
const char eepromMagic[20] = "INKPLATE PHOTOFRAME";
#define EEPROM_PHOTO_INDEX_LIST sizeof(eepromMagic)
uint16_t photoIndexList[MAX_PHOTOS];
#define EEPROM_PHOTO_COUNT (EEPROM_PHOTO_INDEX_LIST + sizeof(photoIndexList))
uint16_t photoCount;
#define EEPROM_NEXT_PHOTO_INDEX (EEPROM_PHOTO_COUNT + sizeof(photoCount))
uint16_t nextPhotoIndex;

#ifndef TINYPICO_WAVESHARE_EPD
static uint8_t displayObjStorage[sizeof(Inkplate)];
Inkplate *display;
#else
static uint8_t displayObjStorage[sizeof(Adafruit_ACEP_PSRAM)];
SPIClass vspi_class(VSPI);
SPIClass hspi_class(HSPI);
Adafruit_ACEP_PSRAM *display;
SdSpiConfig sdConfig(SD_CS, SHARED_SPI, SPI_HALF_SPEED, &hspi_class);
SdFat sd;
TinyPICO tp = TinyPICO();
#endif

SdFile file;
SdFile dir;

void checkBattery()
{
#ifndef TINYPICO_WAVESHARE_EPD
  double batteryLevel = display->readBattery();
#else
  float batteryLevel = tp.GetBatteryVoltage();
#endif
  Serial.print("Battery level: ");
  Serial.println(batteryLevel);
#ifndef ALWAYS_SHOW_BATTERY
  if (batteryLevel < BATTERY_WARNING_LEVEL)
  {
#endif
#ifndef TINYPICO_WAVESHARE_EPD
    display->setTextColor(7, 0);
#else
  display->setTextColor(ACEP_COLOR_WHITE, ACEP_COLOR_BLACK);
#endif
    display->setCursor(0, E_INK_HEIGHT - 26);
    display->print("Battery level low! (");
    display->print(batteryLevel);
    display->println(")");
#ifndef ALWAYS_SHOW_BATTERY
  }
#endif
}

void gotoSleep()
{
  Serial.println("Waiting 2.5s for everything to settle...");
  delay(2500);
  Serial.println("Going to sleep");

#ifndef TINYPICO_WAVESHARE_EPD
  rtc_gpio_isolate(GPIO_NUM_12);
#endif
  // Isolate/disable GPIO12 on ESP32 (only to reduce power consumption in sleep)
  esp_sleep_enable_timer_wakeup(uS_TO_SLEEP); // Activate wake-up timer
  esp_deep_sleep_start();                     // Put ESP32 into deep sleep. Program stops here.
}

void buildIndex()
{
  nextPhotoIndex = 0;
  photoCount = 0;

  dir.rewind();
  while (true)
  {
    Serial.print("Scanning file: ");
    if (!file.openNext(&dir, O_RDONLY))
    {
      Serial.println("End reached!");
      Serial.print("Scanned ");
      Serial.print(photoCount);
      Serial.println(" photos");
      return;
    }

    Serial.print(file.dirIndex());
    Serial.print(", ");
#ifndef TINYPICO_WAVESHARE_EPD
    file.printName();
#else
    file.printName(&Serial);
#endif
    Serial.println();

    if (file.isDir())
    {
      Serial.println("Found directory. Skipping.");
      file.close();
      continue;
    }

    if (file.isHidden())
    {
      Serial.println("Found hidden file. Skipping.");
      file.close();
      continue;
    }

    photoIndexList[photoCount++] = file.dirIndex();
    file.close();

    if (photoCount >= MAX_PHOTOS)
    {
      Serial.print("Max photo count of ");
      Serial.print(MAX_PHOTOS);
      Serial.println(" reached. Stopping scan.");
      break;
    }
  }
}

void shuffleArray(uint16_t *array, uint16_t size)
{
  uint16_t last = 0;
  uint16_t temp = array[last];
  for (uint16_t i = 0; i < size; i++)
  {
    uint16_t index = random(0, size);
    array[last] = array[index];
    last = index;
  }
  array[last] = temp;
}

void shuffleIndex()
{
  shuffleArray(photoIndexList, photoCount);
}

void invalidateEEPROM()
{
  EEPROM.put(EEPROM_MAGIC, "INVALID");
  EEPROM.commit();
}

void updateEEPROM()
{
  EEPROM.put(EEPROM_MAGIC, eepromMagic);
  EEPROM.put(EEPROM_PHOTO_INDEX_LIST, photoIndexList);
  EEPROM.put(EEPROM_PHOTO_COUNT, photoCount);
  EEPROM.put(EEPROM_NEXT_PHOTO_INDEX, nextPhotoIndex);
  EEPROM.commit();
}

void readEEPROM()
{
  Serial.println("Reading EEPROM...");
  EEPROM.get(EEPROM_PHOTO_INDEX_LIST, photoIndexList);
  EEPROM.get(EEPROM_PHOTO_COUNT, photoCount);
  EEPROM.get(EEPROM_NEXT_PHOTO_INDEX, nextPhotoIndex);
}

void initEEPROM()
{
  char magic[20];

  if (!EEPROM.begin(MAX_PHOTOS * 2 /*photoIndexList*/ + 2 /*photoIndexCount*/ + 2 /*nextPhotoIndex*/ + 20 /*eepromMagic*/))
  {
    display->println("EEPROM initialization error!");
    Serial.println("EEPROM initialization error!");
    display->display();
    gotoSleep();
  }

  EEPROM.get(EEPROM_MAGIC, magic);
  if (strncmp(magic, eepromMagic, 20) != 0)
  {
    Serial.println("No valid config found formatting EEPROM.");
    buildIndex();
    shuffleIndex();
    updateEEPROM();
  }
}

void initSd()
{
  uint8_t retries = 5;
  uint8_t retry_delay = 100;
#ifndef TINYPICO_WAVESHARE_EPD
  while (!display->sdCardInit() && retries > 0)
  {
#else
  while (!sd.begin(sdConfig) && retries > 0)
  {
#endif
    Serial.println("SD initialization error, retrying!");
    --retries;
    delay(retry_delay);
  }
  if (retries == 0)
  {
    display->println("SD initialization error!");
    Serial.println("SD initialization error!");
    display->display();
    invalidateEEPROM();
    gotoSleep();
    return;
  }
  else
  {
    Serial.println("SD Initialized.");
  }
}

void openPhotoDirectory()
{
  if (dir.open("/photos") == 0)
  {
    display->println("Could not open 'photos' folder.");
    Serial.println("Could not open 'photos' folder.");
    display->display();
    invalidateEEPROM();
    gotoSleep();
    return;
  }
  else
  {
    Serial.println("Directory opened.");
  }
}

void readAndDisplayPhoto()
{
  const uint16_t width = E_INK_WIDTH / 2;
  // const uint16_t height = 600;

  uint32_t offset = 0;
  uint16_t nBytes;
  uint8_t buffer[1024];
  uint16_t x, y;
  uint16_t i;
  uint32_t total = 0;

  if (!file.open(&dir, photoIndexList[nextPhotoIndex], O_RDONLY))
  {
    Serial.println("Could not open picture file.");
    display->println("Could not open picture file.");
    display->display();
    invalidateEEPROM();
    gotoSleep();
    return;
  }

  memset(&buffer, 0, 1024);
  nBytes = file.read(&buffer, 1024);
  while (nBytes > 0)
  {
    for (i = 0; i < nBytes; i++)
    {
      y = (offset + i) / width;
      x = ((offset + i) % width) * 2;
#ifndef TINYPICO_WAVESHARE_EPD
      display->drawPixel(x, y, (buffer[i] >> 4) >> 1);
      display->drawPixel(x + 1, y, (buffer[i] & 0x0f) >> 1);
#else
      display->writePixel(x, y, buffer[i] >> 4 & 0x0f);
      display->writePixel(x + 1, y, buffer[i] & 0x0f);
#endif
    }
    offset += nBytes;
    total += nBytes;
    nBytes = file.read(&buffer, 1024);
  }
  Serial.print("Read image bytes: ");
  Serial.println(total);
  file.close();
}

void setup()
{
  Serial.begin(115200);
  while (!Serial)
  {
    delay(1);
  }

#ifndef TINYPICO_WAVESHARE_EPD
  display = new (displayObjStorage) Inkplate(INKPLATE_3BIT);
#ifdef USE_INKPLATE_LIGHTMODE
  display->begin(true);
#else
  display->begin();
#endif
  display->setTextSize(3);
  display->setTextColor(0, 7);
  display->setTextWrap(true);
#else
  // Spi for sdcard
  hspi_class.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);

  // Disable APA102
  pinMode(APA_102_PWR, OUTPUT);
  digitalWrite(APA_102_PWR, 0);

  display = new (displayObjStorage) Adafruit_ACEP_PSRAM(E_INK_WIDTH, E_INK_HEIGHT, EPD_DC, EPD_RESET, EPD_CS, EPD_BUSY, &vspi_class);
  display->begin();
  display->clearBuffer();
  display->setTextSize(3);
  display->setTextColor(ACEP_COLOR_BLACK, ACEP_COLOR_WHITE);
  display->setTextWrap(true);
#endif

  initSd();
  openPhotoDirectory();
  initEEPROM();
  readEEPROM();

  readAndDisplayPhoto();
  if (nextPhotoIndex >= photoCount - 1)
  {
    // Reshuffle and reset for next run needed
    Serial.println("End of Photos reached. Reindexing and Reshuffling...");
    buildIndex();
    shuffleIndex();
  }
  else
  {
    ++nextPhotoIndex;
  }
  updateEEPROM();
  checkBattery();

  display->display();
  gotoSleep();
}

void loop()
{
  // Nothing here due to deep sleep.
}
