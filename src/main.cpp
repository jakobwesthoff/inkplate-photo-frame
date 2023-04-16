#if defined(ARDUINO_INKPLATE10) || defined(ARDUINO_INKPLATE) || defined(ARDUINO_INKPLATECOLOR)
#include "Inkplate.h"
#include "inkplate_battery.h"
#else
#include <SPI.h>
#include "Adafruit_ACEP_PSRAM.h"
#include <TinyPICO.h>
#endif

#include "SdFat.h"
#include "driver/rtc_io.h"

// Uncomment this line, if you have one of the newer inkplate 10s, which have a
// different (darker) color spectrum.
// #define USE_INKPLATE_LIGHTMODE

// #define ALWAYS_SHOW_BATTERY
#define BATTERY_WARNING_LEVEL 3.6

// #define uS_TO_SLEEP 10800000000 // 3h
// #define uS_TO_SLEEP 5400000000 //1.5h
// #define uS_TO_SLEEP 2700000000 //45m
// #define uS_TO_SLEEP 10000000 // 5s
#define uS_TO_SLEEP 15 * 60 * 1000 * 1000

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

typedef struct photo_index
{
  uint16_t dir_index;
  uint16_t file_index;
} photo_index_t;

#define MAX_PHOTOS 32767
const char config_magic[20] = "INKPLATE PHOTOFRAME";
#define CONFIG_MAGIC_LEN sizeof(config_magic)
// Allocated in psram
photo_index_t *photo_index_list;
#define CONFIG_PHOTO_INDEX_LEN (sizeof(photo_index_t) * MAX_PHOTOS)
uint16_t photo_count;
#define CONFIG_PHOTO_COUNT_LEN sizeof(photo_count)
uint16_t next_photo_index;
#define CONFIG_NEXT_PHOTO_INDEX_LEN sizeof(next_photo_index)

#define HARD_ERROR(x) { \
    display->println(x); \
    log_d(x); \
    display->display(); \
    goto_sleep(uS_TO_SLEEP); \
    return; \
}

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

SdFile photos_dir;
SdFile config;

void check_battery()
{
#ifndef TINYPICO_WAVESHARE_EPD
  double batteryLevel = readInkplateBattery(display);
#else
  float batteryLevel = tp.GetBatteryVoltage();
#endif
  log_d("Battery level: %lf", batteryLevel);
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

void goto_sleep(uint64_t micro_seconds)
{
  log_d("Going to sleep");

#ifndef TINYPICO_WAVESHARE_EPD
  // Isolate/disable GPIO12 on ESP32 (only to reduce power consumption in sleep)
  rtc_gpio_isolate(GPIO_NUM_12);
#endif
  // Isolate/disable GPIO12 on ESP32 (only to reduce power consumption in sleep)
  esp_sleep_enable_timer_wakeup(uS_TO_SLEEP); // Activate wake-up timer
  esp_deep_sleep_start();                     // Put ESP32 into deep sleep. Program stops here.
}

void log_wakeup_reason()
{
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();
  switch (wakeup_reason)
  {
  case ESP_SLEEP_WAKEUP_EXT0:
    log_d("Wakeup caused by external signal using RTC_IO");
    break;
  case ESP_SLEEP_WAKEUP_EXT1:
    log_d("Wakeup caused by external signal using RTC_CNTL");
    break;
  case ESP_SLEEP_WAKEUP_TIMER:
    log_d("Wakeup caused by timer");
    break;
  case ESP_SLEEP_WAKEUP_TOUCHPAD:
    log_d("Wakeup caused by touchpad");
    break;
  case ESP_SLEEP_WAKEUP_ULP:
    log_d("Wakeup caused by ULP program");
    break;
  default:
    log_d("Wakeup was not caused by deep sleep");
    break;
  }
}

void build_index_for_dir(SdFile *dir)
{
  char dirname[256];
  SdFile file;

  dir->getName(dirname, sizeof(dirname));
  log_d("Rebuilding index for %s", dirname);

  dir->rewind();
  while (true)
  {
    if (!file.openNext(dir, O_RDONLY))
    {
      log_d("End reached of %s", dirname);
      return;
    }

    if (file.isDir())
    {
      log_d("Found sub/sub directory. Skipping.");
      file.close();
      continue;
    }

    if (file.isHidden())
    {
      log_d("Found hidden file. Skipping.");
      file.close();
      continue;
    }

    photo_index_list[photo_count++] = {.dir_index = dir->dirIndex(), .file_index = file.dirIndex()};
    file.close();

    if (photo_count >= MAX_PHOTOS)
    {
      log_d("Max photo count of %d reached. Stopping scan.", MAX_PHOTOS);
      return;
    }
  }
}

void build_index()
{
  next_photo_index = 0;
  photo_count = 0;

  SdFile file;
  log_d("Rebuilding /photos index");

  photos_dir.rewind();
  while (true)
  {
    if (!file.openNext(&photos_dir, O_RDONLY))
    {
      log_d("End reached of /photos");
      return;
    }

    if (!file.isDir())
    {
      log_d("Found non directory in /photos. Skipping.");
      file.close();
      continue;
    }

    build_index_for_dir(&file);
    file.close();

    if (photo_count >= MAX_PHOTOS)
    {
      break;
    }
  }

  log_d("Finished rebuilding. Scanned %d photos", photo_count);
}

void shuffle_array(photo_index_t *array, uint16_t size)
{
  // Initialize randomseed using internal random generation of esp32
  randomSeed(analogRead(0));

  uint16_t last = 0;
  photo_index_t temp = array[last];
  for (uint16_t i = 0; i < size; i++)
  {
    uint16_t index = random(0, size);
    array[last] = array[index];
    last = index;
  }
  array[last] = temp;
}

void shuffle_index()
{
  log_d("Shuffle index...");
  shuffle_array(photo_index_list, photo_count);
}

void open_config()
{
  if (config.open("/config.bin", FILE_WRITE) == 0)
  {
    HARD_ERROR("Could not open '/config.bin'")
  }
  else
  {
    log_d("/config.bin opened.");
  }
  config.rewind();
}

void open_config_tmp(SdFile *config)
{
  if (config->open("/~config.bin", FILE_WRITE) == 0)
  {
    HARD_ERROR("Could not open '/~config.bin'")
  }
  else
  {
    log_d("/~config.bin opened.");
  }
}

void update_config()
{
  SdFile new_config;

  log_d("Updating config...");
  open_config_tmp(&new_config);
  new_config.truncate(0);
  new_config.rewind();
  new_config.write(config_magic, CONFIG_MAGIC_LEN);
  new_config.write(photo_index_list, CONFIG_PHOTO_INDEX_LEN);
  new_config.write(&photo_count, CONFIG_PHOTO_COUNT_LEN);
  new_config.write(&next_photo_index, CONFIG_NEXT_PHOTO_INDEX_LEN);
  new_config.flush();
  log_d("New config written.");
  if (config.remove() == false) {
    HARD_ERROR("Could not remove old config file for update")
  };
  config.close();

  if (new_config.rename("/config.bin") == false) {
    HARD_ERROR("Could not replace new config with old one")
  }
  new_config.close();
  open_config();
  log_d("config update complete");
}

void read_config()
{
  char magic[CONFIG_MAGIC_LEN];

  log_d("Reading config...");
  config.rewind();
  config.read(magic, CONFIG_MAGIC_LEN);
  config.read(photo_index_list, CONFIG_PHOTO_INDEX_LEN);
  config.read(&photo_count, CONFIG_PHOTO_COUNT_LEN);
  config.read(&next_photo_index, CONFIG_NEXT_PHOTO_INDEX_LEN);
}

void init_config()
{
  char magic[CONFIG_MAGIC_LEN];

  open_config();

  config.read(magic, CONFIG_MAGIC_LEN);
  if (strncmp(magic, config_magic, 20) != 0)
  {
    log_d("No valid config found reinitializing it.");
    build_index();
    shuffle_index();
    update_config();
  }
}

void init_sd()
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
    log_d("SD initialization error, retrying!");
    --retries;
    delay(retry_delay);
  }
  if (retries == 0)
  {
    HARD_ERROR("SD initialization error!")
  }
  else
  {
    log_d("SD Initialized.");
  }
}

void open_photo_directory()
{
  if (photos_dir.open("/photos") == 0)
  {
    HARD_ERROR("Could not open 'photos' folder.")
  }
  else
  {
    log_d("Directory opened.");
  }
}

void read_and_display_photo()
{
  SdFile dir;
  SdFile file;
  const uint16_t width = E_INK_WIDTH / 2;
  // const uint16_t height = 600;

  uint32_t offset = 0;
  uint16_t n_bytes;
  uint8_t buffer[1024];
  uint16_t x, y;
  uint16_t i;
  uint32_t total = 0;

  // for (uint16_t photo_idx = 0; photo_idx < photo_count; photo_idx++)
  // {
  //   uint16_t dir_index = (photo_index_list[photo_idx] & 0xffff0000) >> 16;
  //   uint16_t file_index = (photo_index_list[photo_idx] & 0x0000ffff);
  //   log_d("Opening id : [%d] %d, %d", photo_idx, dir_index, file_index);

  //   if (dir.open(&photos_dir, dir_index, 0) == 0)
  //   {
  //     log_d("Could not open picture file directory.");
  //     display->println("Could not open picture file directory.");
  //     display->display();
  //     invalidate_config();
  //     goto_sleep(uS_TO_SLEEP);
  //     return;
  //   }

  //   if (!file.open(&dir, file_index, O_RDONLY))
  //   {
  //     log_d("Id [%d] %d %d could not be opened!", photo_idx, dir_index, file_index);
  //   }
  //   else
  //   {
  //     file.close();
  //   }
  //   dir.close();
  // }

  photo_index_t photo_index = photo_index_list[next_photo_index];

  if (dir.open(&photos_dir, photo_index.dir_index, 0) == 0)
  {
    HARD_ERROR("Could not open picture file directory.")
  }

  if (!file.open(&dir, photo_index.file_index, O_RDONLY))
  {
    HARD_ERROR("Could not open picture file.");
  }

  memset(&buffer, 0, 1024);
  n_bytes = file.read(&buffer, 1024);
  while (n_bytes > 0)
  {
    for (i = 0; i < n_bytes; i++)
    {
      y = (offset + i) / width;
      x = ((offset + i) % width) * 2;
#ifdef TINYPICO_WAVESHARE_EPD
      display->writePixel(x, y, buffer[i] >> 4 & 0x0f);
      display->writePixel(x + 1, y, buffer[i] & 0x0f);
#elif ARDUINO_INKPLATECOLOR
      display->drawPixel(x, y, (buffer[i] >> 4));
      display->drawPixel(x + 1, y, (buffer[i] & 0x0f));
#else
      display->drawPixel(x, y, (buffer[i] >> 4) >> 1);
      display->drawPixel(x + 1, y, (buffer[i] & 0x0f) >> 1);
#endif
    }
    offset += n_bytes;
    total += n_bytes;
    n_bytes = file.read(&buffer, 1024);
  }
  log_d("Read image bytes: %d", total);
  file.close();
  dir.close();
}

void setup()
{
  Serial.begin(115200);
  while (!Serial)
  {
    delay(1);
  }

  log_wakeup_reason();

#ifndef TINYPICO_WAVESHARE_EPD
#ifndef ARDUINO_INKPLATECOLOR
  display = new (displayObjStorage) Inkplate(INKPLATE_3BIT);
#else
  display = new (displayObjStorage) Inkplate();
#endif
#if defined(USE_INKPLATE_LIGHTMODE) && !defined(ARDUINO_INKPLATE_COLOR)
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
  // Check PSRAM is working
  log_d("Total heap: %d", ESP.getHeapSize());
  log_d("Free heap: %d", ESP.getFreeHeap());
  log_d("Total PSRAM: %d", ESP.getPsramSize());
  log_d("Free PSRAM: %d", ESP.getFreePsram());

  // Allocate psram buffer for photoindex;
  photo_index_list = (photo_index_t *)ps_malloc(CONFIG_PHOTO_INDEX_LEN);
  if (photo_index_list == nullptr)
  {
    HARD_ERROR("Allocation of photo_index memory failed!");
  }

  init_sd();
  open_photo_directory();
  init_config();
  read_config();

  read_and_display_photo();
  if (next_photo_index >= photo_count - 1)
  {
    // Reshuffle and reset for next run needed
    log_d("End of Photos reached. Reindexing and Reshuffling...");
    build_index();
    shuffle_index();
  }
  else
  {
    ++next_photo_index;
  }
  update_config();
  check_battery();

  display->display();
  goto_sleep(uS_TO_SLEEP);
}

void loop()
{
  // Nothing here due to deep sleep.
}
