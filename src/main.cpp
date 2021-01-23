#include "Inkplate.h"
#include "SdFat.h"
#include "EEPROM.h"
#include "driver/rtc_io.h"

//#define ALWAYS_SHOW_BATTERY
#define BATTERY_WARNING_LEVEL 4.1

#define uS_TO_SLEEP 10800000000 //3h
//#define uS_TO_SLEEP 5400000000 //1.5h
//#define uS_TO_SLEEP 2700000000 //45m
//#define uS_TO_SLEEP 10000000 //10s


#define MAX_PHOTOS 2048
#define EEPROM_MAGIC 0
const char eepromMagic[20] = "INKPLATE PHOTOFRAME";
#define EEPROM_PHOTO_INDEX_LIST sizeof(eepromMagic)
uint16_t photoIndexList[MAX_PHOTOS];
#define EEPROM_PHOTO_COUNT (EEPROM_PHOTO_INDEX_LIST + sizeof(photoIndexList))
uint16_t photoCount;
#define EEPROM_NEXT_PHOTO_INDEX (EEPROM_PHOTO_COUNT + sizeof(photoCount))
uint16_t nextPhotoIndex;

Inkplate display(INKPLATE_3BIT);
SdFile file;
SdFile dir;

void checkBattery() {
  double batteryLevel = display.readBattery();
  Serial.print("Battery level: ");
  Serial.println(batteryLevel);
  #ifndef ALWAYS_SHOW_BATTERY
  if (batteryLevel < BATTERY_WARNING_LEVEL) {
  #endif
    display.setTextColor(7, 0);
    display.setCursor(0,574);
    display.print("Battery level low! (");
    display.print(batteryLevel);
    display.println(")");
  #ifndef ALWAYS_SHOW_BATTERY
  }
  #endif
}

void gotoSleep()
{
  Serial.println("Waiting 2.5s for everything to settle...");
  delay(2500);
  Serial.println("Going to sleep");
  rtc_gpio_isolate(GPIO_NUM_12);              //Isolate/disable GPIO12 on ESP32 (only to reduce power consumption in sleep)
  esp_sleep_enable_timer_wakeup(uS_TO_SLEEP); //Activate wake-up timer
  esp_deep_sleep_start();                     //Put ESP32 into deep sleep. Program stops here.
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
    file.printName();
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

    if (photoCount >= MAX_PHOTOS) {
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

  if (!EEPROM.begin(MAX_PHOTOS*2 /*photoIndexList*/ + 2 /*photoIndexCount*/ + 2 /*nextPhotoIndex*/ + 20 /*eepromMagic*/))
  {
    display.println("EEPROM initialization error!");
    Serial.println("EEPROM initialization error!");
    display.display();
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
  if (!display.sdCardInit())
  {
    display.println("SD initialization error!");
    Serial.println("SD initialization error!");
    display.display();
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
    display.println("Could not open 'photos' folder.");
    Serial.println("Could not open 'photos' folder.");
    display.display();
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
  const uint16_t width = 800 / 2;
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
    display.println("Could not open picture file.");
    display.display();
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
      display.drawPixel(x, y, (buffer[i] >> 4) >> 1);
      display.drawPixel(x + 1, y, (buffer[i] & 0x0f) >> 1);
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

  display.begin();
  display.setTextSize(3);
  display.setTextColor(0, 7);
  display.setTextWrap(true);

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
  display.display();
  gotoSleep();
}

void loop()
{
  //Nothing here due to deep sleep.
}
