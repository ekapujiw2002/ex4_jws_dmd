// main arduino header
// komentar header ini kalau menggunakan IDE ARDUINO
#include <Arduino.h>

// timer1 library
// #include <TimerOne.h>

// i2c lib
#include <Wire.h>

// spi lib
// #include <SPI.h>

// dmd version used
#define DMD_VERSION 3
#if DMD_VERSION == 1
#include <DMD.h>
#elif DMD_VERSION == 2
#include <DMD2.h>
#else
#include <DMD3.h>
#endif

// common font
#include <Arial_black_16.h>
#include <SystemFont5x7.h>

// rtc ds3231 library
#include <Sodaq_DS3231.h>

const char *day_locale[] = {"Minggu", "Senin", "Selasa", "Rabu",
                            "Kamis",  "Jumat", "Sabtu"};

// prayer time lib
#include <PrayerTimes.h>

// global var
/*
 * PIN YANG DISAMBUNG
 * Arduino              P-10
 * -D6          ->      -A
 * -D7          ->      -B
 * -D8          ->      -SCLK
 * -D9          ->      -OE
 * -D11         ->      -DATA
 * -D13         ->      -CLK
 * -GND         ->      -GND
 */
#define DISPLAY_COL_NUM 3
#define DISPLAY_ROW_NUM 1
#if DMD_VERSION == 1
DMD display(DISPLAY_COL_NUM,
            DISPLAY_ROW_NUM); // untuk mengatur jumlah panel yang kamu pakai
#elif DMD_VERSION == 2
DMD2 display(DISPLAY_COL_NUM,
             DISPLAY_ROW_NUM); // untuk mengatur jumlah panel yang kamu pakai
#else
#define display_oe_pin 9
DMD3 display(DISPLAY_COL_NUM,
             DISPLAY_ROW_NUM); // untuk mengatur jumlah panel yang kamu pakai
#endif

// struct utk setting jws
typedef struct { double lat, lon, timezone; } jws_location_setting_t;
jws_location_setting_t jws_loc_set = {-7.0667, 110.4, 7};

// prayer time var
double prayer_times[sizeof(TimeName) / sizeof(char *)];

// messages
const char *msg_atas[] = {"Selamat datang di Masjid xxx Desa xxx",
                          "Matikan hp saat memasuki masjid",
                          "Rapatkan dan luruskan shaf,sesungguhnya rapat dan  "
                          "Lurusnya shaf keutamaan dalam shalat berjamaah",
                          "Haram bicara saat khotib menyampaikan khotbah"};

// delay scanrate in ms
uint8_t display_delay_rate[2] = {70};

// last refresh time
uint32_t display_last_refresh[2] = {0};

// total scroll length
int16_t display_total_step[2] = {64}, display_current_step[2] = {0};

// text display
String display_text[2];

// need swapped??
bool display_must_swapped = false;

// time var
uint32_t t_now, t_last_updated;

/**
 * update the display
 * @method display_scan
 */
void display_scan() { display.refresh(); }

/**
 * setup timer1
 * @method timer1_init
 */
void timer1_init() {
  Timer1.initialize(2000);
  Timer1.attachInterrupt(display_scan);
  // brightness
  Timer1.pwm(display_oe_pin, 25);
}

/**
 * init display dmd
 * @method display_init
 */
void display_init() {
  display.setDoubleBuffer(true);
  display.clear();
}

/**
 * init i2c and rtc
 * @method i2c_rtc_init
 */
void i2c_rtc_init() {
  // init i2c device
  Wire.begin();

  // init rtc
  rtc.begin();
}

/**
 * ini prayer time setting Calculation
 * @method prayer_calc_init
 */
void prayer_calc_init() {
  set_calc_method(ISNA);  // Methode perhitungan mengikuti ISNA
  set_asr_method(Shafii); // Pendapat Imam Syafi'i
  set_high_lats_adjust_method(AngleBased);
  set_fajr_angle(20); // Sudut Waktu Subuh
  set_isha_angle(18); ////Sudut Waktu Isya
}

/**
 * calculate prayer times
 * @method prayer_time_update
 * @param  rtc_in             [description]
 * @param  jws_set            [description]
 * @param  times_out          [description]
 */
void prayer_time_update(const DateTime rtc_in,
                        const jws_location_setting_t jws_set,
                        double *times_out) {
  get_prayer_times(rtc_in.year(), rtc_in.month(), rtc_in.date(), jws_set.lat,
                   jws_set.lon, jws_set.timezone, times_out);
}

/**
 * check what prayer time is it now
 * @method prayer_time_check_event
 * @param  pryr_times_input        [description]
 * @return                         [description]
 */
uint8_t prayer_time_check_event(DateTime current_time,
                                double *pryr_times_input) {
  uint8_t retx = 255;
  bool is_event_detected = false;
  int hhx, mmx;

  // 0=imsak
  // 7=Isya
  for (size_t i = 0; i < 8; i++) {
    if (i == 0) {
      // check waktu imsak
      // kode hasil = 0
      get_float_time_parts(pryr_times_input[i] - (10.0f / 60.0f), hhx, mmx);
    } else {
      get_float_time_parts(pryr_times_input[i - 1], hhx, mmx);
    }

    is_event_detected =
        ((hhx == current_time.hour()) && (current_time.minute() - mmx < 5));
    if (is_event_detected) {
      retx = i;
      break;
    }
  }

  return retx;
}

/**
 * update text ke display
 * @method display_update_text_process
 */
void display_update_text_process() {
  // top text
  if (display_current_step[0] == 0) {
    display_text[0] = String(msg_atas[t_last_updated % 4]) + String(" ");
  }

  // bottom text
  if (display_current_step[1] == 0) {
    DateTime tnx = rtc.now();
    char tmx[16];

    // current date
    snprintf_P(tmx, sizeof(tmx), "%02u-%02u-%04u", tnx.date(), tnx.month(),
               tnx.year());
    display_text[1] = String(day_locale[tnx.dayOfWeek() - 1]) + String(",") +
                      String(tmx) + String(" ");

    // prayer time update
    prayer_time_update(tnx, jws_loc_set, prayer_times);

    // build up text
    int hhx, mmx;
    // waktu 1/3 malam
    // terbit-((terbit-tenggelam)/3)

    // imsak = subuh - 10menit
    get_float_time_parts(prayer_times[0] - (10.0f / 60.0f), hhx, mmx);
    snprintf_P(tmx, sizeof(tmx), "%02d:%02d", hhx, mmx);
    display_text[1] += String("Imsak ") + String(tmx) + String(" ");

    // Subuh
    get_float_time_parts(prayer_times[0], hhx, mmx);
    snprintf_P(tmx, sizeof(tmx), "%02d:%02d", hhx, mmx);
    display_text[1] += String("Subuh ") + String(tmx) + String(" ");

    // terbit
    get_float_time_parts(prayer_times[1], hhx, mmx);
    snprintf_P(tmx, sizeof(tmx), "%02d:%02d", hhx, mmx);
    display_text[1] += String("Terbit ") + String(tmx) + String(" ");

    // dzuhur
    get_float_time_parts(prayer_times[2], hhx, mmx);
    snprintf_P(tmx, sizeof(tmx), "%02d:%02d", hhx, mmx);
    display_text[1] += String("Dzuhur ") + String(tmx) + String(" ");

    // Ashar
    get_float_time_parts(prayer_times[3], hhx, mmx);
    snprintf_P(tmx, sizeof(tmx), "%02d:%02d", hhx, mmx);
    display_text[1] += String("Ashar ") + String(tmx) + String(" ");

    // Maghrib
    get_float_time_parts(prayer_times[5], hhx, mmx);
    snprintf_P(tmx, sizeof(tmx), "%02d:%02d", hhx, mmx);
    display_text[1] += String("Maghrib ") + String(tmx) + String(" ");

    // Isya
    get_float_time_parts(prayer_times[6], hhx, mmx);
    snprintf_P(tmx, sizeof(tmx), "%02d:%02d", hhx, mmx);
    display_text[1] += String("Isya ") + String(tmx) + String(" ");
  }
}

/**
 * app setup
 * @method setup
 */
void setup() {
  Serial.begin(9600);
  // Serial.println(msg_atas[0]);

  // init timer1 for 2000us scan rate display
  timer1_init();

  // init rtc and i2c device
  i2c_rtc_init();

  prayer_calc_init();

  // set display option
  display_init();
  //
  // display.setFont(System5x7);
  // display.drawText(0, 0, msg_atas[0]);

  // init the text
  display_text[0] = String(msg_atas[0]);
  display_text[1] = String(msg_atas[2]);
}

/**
 * main loop eternal
 * @method loop
 */
void loop() {
  // current millis
  t_now = millis();

  // calculate time and text update
  if (t_now - t_last_updated > 1000) {
    t_last_updated = t_now;
    display_update_text_process();
  }

  // update the step
  for (uint8_t i = 0; i < 2; i++) {
    if (t_now >= display_last_refresh[i]) {
      if (display_current_step[i] == 0) {
        display_update_text_process();
        display_total_step[i] =
            display.textWidth(display_text[i]) + display.width();
      }
      display_last_refresh[i] = t_now + display_delay_rate[i];
      display_current_step[i] < display_total_step[i]
          ? display_current_step[i]++
          : (display_current_step[i] = 0);
      display_must_swapped = true;
    }
  }

  // display the text
  if (display_must_swapped) {
    display_must_swapped = false;

    display.clear();
    display.setFont(System5x7);
    display.drawText(display.width() - display_current_step[0], 0,
                     display_text[0]);
    display.drawText(display.width() - display_current_step[1], 8,
                     display_text[1]);
    display.swapBuffers();
  }
}
