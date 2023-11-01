/* Roulette app */
/* Copyright ©2019 - 23 Adrian Kennard, Andrews & Arnold Ltd.See LICENCE file for details .GPL 3.0 */

static __attribute__((unused))
     const char TAG[] = "Roulette";

#include "revk.h"
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include <driver/gpio.h>
#include <driver/uart.h>
#include <driver/rtc_io.h>
#include "led_strip.h"
#include <esp_http_server.h>
#include <math.h>

     static httpd_handle_t webserver = NULL;


     RTC_NOINIT_ATTR uint8_t n; // Current number 0-(n-1) - remembered between runs

     const uint8_t num[] =
        { 0, 32, 15, 19, 4, 21, 2, 25, 17, 34, 6, 37, 13, 36, 11, 30, 8, 23, 10, 5, 24, 16, 33, 1, 20, 14, 31, 9, 22, 18, 29, 7, 28,
        12, 35, 3, 26
     };

#define	N	(2*sizeof(num)/sizeof(*num))
#define	LEDS	(N+7*11)

const uint8_t digit[][7] = {
   {0x70, 0x88, 0x88, 0x88, 0x88, 0x88, 0x70},
   {0x20, 0x60, 0x20, 0x20, 0x20, 0x20, 0x70},
   {0x70, 0x88, 0x08, 0x30, 0x40, 0x80, 0xF8},
   {0xF8, 0x08, 0x10, 0x30, 0x08, 0x88, 0x70},
   {0x10, 0x30, 0x50, 0x90, 0xF8, 0x10, 0x10},
   {0xF8, 0x80, 0xF0, 0x08, 0x08, 0x88, 0x70},
   {0x30, 0x40, 0x80, 0xF0, 0x88, 0x88, 0x70},
   {0xF8, 0x08, 0x10, 0x20, 0x40, 0x40, 0x40},
   {0x70, 0x88, 0x88, 0x70, 0x88, 0x88, 0x70},
   {0x70, 0x88, 0x88, 0x78, 0x08, 0x10, 0x60},
};

#define	settings	\
	b(webcontrol)	\
	io(btn1,-7)	\
	io(btn2,-42)	\
	io(pwr,-4)	\
	io(rgb,5)	\
	io(charge,-11)	\
	io(adc,14)	\
	io(adcfet,-15)	\

#define	IO_MASK	0x3F
#define	IO_INV	0x40

#define u32(n,d)        uint32_t n;
#define u32l(n,d)        uint32_t n;
#define s8(n,d) int8_t n;
#define s8n(n,d) int8_t n[d];
#define u8(n,d) uint8_t n;
#define u8r(n,d) RTC_NOINIT_ATTR uint8_t n;
#define u16(n,d) uint16_t n;
#define u8l(n,d) uint8_t n;
#define b(n) uint8_t n;
#define s(n,d) char * n;
#define io(n,d)           RTC_NOINIT_ATTR uint8_t n;
#ifdef  CONFIG_REVK_BLINK
#define led(n,a,d)      extern uint8_t n[a];
#else
#define led(n,a,d)      uint8_t n[a];
#endif
settings
#undef led
#undef io
#undef u32
#undef u32l
#undef s8
#undef s8n
#undef u8
#undef u8r
#undef u16
#undef s8r
#undef u8l
#undef b
#undef s
const char *
app_callback (int client, const char *prefix, const char *target, const char *suffix, jo_t j)
{
   if (client || !prefix || target || strcmp (prefix, prefixcommand))
      return NULL;              // Not for us or not a command from main MQTT
   return NULL;
}

static inline uint8_t
btnpress (uint8_t b)
{
   if (!b)
      return 0;
   return (gpio_get_level (b & IO_MASK) ? 1 : 0) ^ (b & IO_INV ? 1 : 0);
}

static uint8_t doneinit = 0;

void
night (uint8_t p)
{
   if (doneinit)
      revk_pre_shutdown ();
   if (pwr && rtc_gpio_is_valid_gpio (pwr & IO_MASK))
   {                            // LED power
      gpio_set_level (pwr & IO_MASK, (p ? 1 : 0) ^ (pwr & IO_INV ? 1 : 0));
      rtc_gpio_set_direction_in_sleep (pwr & IO_MASK, RTC_GPIO_MODE_OUTPUT_ONLY);
      rtc_gpio_set_level (pwr & IO_MASK, (p ? 1 : 0) ^ (pwr & IO_INV ? 1 : 0));
      rtc_gpio_hold_dis (pwr & IO_MASK);
      rtc_gpio_isolate (pwr & IO_MASK);
      rtc_gpio_hold_en (pwr & IO_MASK);
   }
   if (btn1 && rtc_gpio_is_valid_gpio (btn1 & IO_MASK))
   {                            // Button 1
      rtc_gpio_set_direction_in_sleep (btn1 & IO_MASK, RTC_GPIO_MODE_INPUT_ONLY);
      rtc_gpio_pullup_dis (btn1 & IO_MASK);
      rtc_gpio_pulldown_dis (btn1 & IO_MASK);
      rtc_gpio_isolate (btn1 & IO_MASK);
   }
   if (btn2 && rtc_gpio_is_valid_gpio (btn2 & IO_MASK))
   {                            // Button 2
      rtc_gpio_set_direction_in_sleep (btn2 & IO_MASK, RTC_GPIO_MODE_INPUT_ONLY);
      rtc_gpio_pullup_dis (btn2 & IO_MASK);
      rtc_gpio_pulldown_dis (btn2 & IO_MASK);
      rtc_gpio_isolate (btn2 & IO_MASK);
   }
   if (charge && rtc_gpio_is_valid_gpio (charge & IO_MASK))
   {                            // Charge
      rtc_gpio_set_direction_in_sleep (charge & IO_MASK, RTC_GPIO_MODE_INPUT_ONLY);
      rtc_gpio_pullup_dis (charge & IO_MASK);
      rtc_gpio_pulldown_dis (charge & IO_MASK);
      rtc_gpio_isolate (charge & IO_MASK);
   }
   // TODO work out "change of state" wake, to include charge
   uint64_t mask = 0;
   if ((btn1 & IO_INV) && rtc_gpio_is_valid_gpio (btn1 & IO_MASK))
      mask |= (1LL << (btn1 & IO_MASK));
   else if ((btn2 & IO_INV) && rtc_gpio_is_valid_gpio (btn2 & IO_MASK))
      mask |= (1LL << (btn2 & IO_MASK));
   if (mask)
      esp_sleep_enable_ext1_wakeup (mask, ESP_EXT1_WAKEUP_ALL_LOW);     // Active low, so one button, annoying
   else
   {
      if (btn1 && rtc_gpio_is_valid_gpio (btn1 & IO_MASK))
         mask |= (1LL << (btn1 & IO_MASK));
      if (btn2 && rtc_gpio_is_valid_gpio (btn2 & IO_MASK))
         mask |= (1LL << (btn2 & IO_MASK));
      esp_sleep_enable_ext1_wakeup (mask, ESP_EXT1_WAKEUP_ANY_HIGH);    // Active high, so any button
   }
   // Go to sleep
   //ESP_LOGE (TAG, "LED power %d (p=%d)", (p ? 1 : 0) ^ (pwr & IO_INV ? 1 : 0), p);
   if (p)
      esp_deep_sleep (10000000LL);      // LEDs on, so timed wait before turning off
   esp_deep_sleep (300000000LL);        // Sleep a while anyway
}

void
init (void)
{
   if (doneinit)
      return;
   ESP_LOGE (TAG, "Start wifi/etc");
   doneinit = 1;
   revk_boot (&app_callback);
#ifndef CONFIG_REVK_BLINK
#define led(n,a,d)      revk_register(#n,a,sizeof(*n),&n,"- "#d,SETTING_SET|SETTING_BITFIELD|SETTING_FIX);
#else
#define led(n,a,d)
#endif
#define io(n,d)           revk_register(#n,0,sizeof(n),&n,"- "#d,SETTING_SET|SETTING_BITFIELD|SETTING_FIX);
#define b(n) revk_register(#n,0,sizeof(n),&n,NULL,SETTING_BOOLEAN);
#define u32(n,d) revk_register(#n,0,sizeof(n),&n,#d,0);
#define u32l(n,d) revk_register(#n,0,sizeof(n),&n,#d,SETTING_LIVE);
#define s8(n,d) revk_register(#n,0,sizeof(n),&n,#d,SETTING_SIGNED);
#define s8n(n,d) revk_register(#n,d,sizeof(*n),&n,NULL,SETTING_SIGNED);
#define u8(n,d) revk_register(#n,0,sizeof(n),&n,#d,0);
#define u8r(n,d) u8(n,d)
#define u16(n,d) revk_register(#n,0,sizeof(n),&n,#d,0);
#define u8l(n,d) revk_register(#n,0,sizeof(n),&n,#d,SETTING_LIVE);
#define s(n,d) revk_register(#n,0,0,&n,#d,0);
   settings
#undef io
#undef u32
#undef u32l
#undef s8
#undef s8n
#undef u8
#undef u8r
#undef u16
#undef s8r
#undef u8l
#undef b
#undef s
      revk_start ();
}

void
app_main ()
{
   esp_reset_reason_t reset = esp_reset_reason ();

   if (!pwr || !rgb || reset == ESP_RST_POWERON || reset == ESP_RST_EXT || reset == ESP_RST_BROWNOUT || reset == ESP_RST_PANIC)
      init ();                  // Get values

   if (btn1)
   {
      if (rtc_gpio_is_valid_gpio (btn1 & IO_MASK))
      {
         rtc_gpio_deinit (btn1 & IO_MASK);
         rtc_gpio_hold_dis (btn1 & IO_MASK);
      }
      gpio_reset_pin (btn1 & IO_MASK);
      gpio_set_direction (btn1 & IO_MASK, GPIO_MODE_INPUT);
   }
   if (btn2)
   {
      if (rtc_gpio_is_valid_gpio (btn2 & IO_MASK))
      {
         rtc_gpio_deinit (btn2 & IO_MASK);
         rtc_gpio_hold_dis (btn2 & IO_MASK);
      }
      gpio_reset_pin (btn2 & IO_MASK);
      gpio_set_direction (btn2 & IO_MASK, GPIO_MODE_INPUT);
   }
   if (charge)
   {
      if (rtc_gpio_is_valid_gpio (charge & IO_MASK))
      {
         rtc_gpio_deinit (charge & IO_MASK);
         rtc_gpio_hold_dis (charge & IO_MASK);
      }
      gpio_reset_pin (charge & IO_MASK);
      gpio_set_direction (charge & IO_MASK, GPIO_MODE_INPUT);
   }
   //ESP_LOGE (TAG, "Ext1 %llX Reset %d BTN1 %X BTN2 %X PWR %X RGB %X Press1 %d Press2 %d", esp_sleep_get_ext1_wakeup_status (), reset, btn1, btn2, pwr, rgb, btnpress (btn1), btnpress (btn2));

   if (!doneinit && !btnpress (btn1) && !btnpress (btn2) && !btnpress (charge) && !esp_sleep_get_ext1_wakeup_status ())
      night (0);                // Off, and sleep

   {                            // All unused input pins pull down
      gpio_config_t c = {.pull_down_en = 1,.mode = GPIO_MODE_DISABLE };
      for (uint8_t p = 0; p <= 48; p++)
         if (gpio_ok (p) & 2)
            c.pin_bit_mask |= (1LL << p);
      if (btn1)
         c.pin_bit_mask &= ~(1LL << (btn1 & IO_MASK));
      if (btn2)
         c.pin_bit_mask &= ~(1LL << (btn2 & IO_MASK));
      if (charge)
         c.pin_bit_mask &= ~(1LL << (charge & IO_MASK));
      if (pwr)
         c.pin_bit_mask &= ~(1LL << (pwr & IO_MASK));
      if (rgb)
         c.pin_bit_mask &= ~(1LL << (rgb & IO_MASK));
      if (adcfet)
         c.pin_bit_mask &= ~(1LL << (adcfet & IO_MASK));
      gpio_config (&c);
   }

   gpio_reset_pin (pwr & IO_MASK);
   gpio_set_direction (pwr & IO_MASK, GPIO_MODE_OUTPUT);
   gpio_set_level (pwr & IO_MASK, (pwr & IO_INV) ? 0 : 1);
   rtc_gpio_hold_dis (pwr & IO_MASK);

   led_strip_handle_t strip = NULL;
   led_strip_config_t strip_config = {
      .strip_gpio_num = rgb & IO_MASK,
      .max_leds = LEDS,         // The number of LEDs in the strip,
      .led_pixel_format = LED_PIXEL_FORMAT_GRB, // Pixel format of your LED strip
      .led_model = LED_MODEL_WS2812,    // LED strip model
   };
   led_strip_rmt_config_t rmt_config = {
      .clk_src = RMT_CLK_SRC_DEFAULT,   // different clock source can lead to different power consumption
      .resolution_hz = 10 * 1000 * 1000,        // 10MHz
      .flags.with_dma = true,
   };
   REVK_ERR_CHECK (led_strip_new_rmt_device (&strip_config, &rmt_config, &strip));
   REVK_ERR_CHECK (led_strip_clear (strip));
   usleep (10000);              // PWR on LEDs

   void digits (int d, uint8_t r, uint8_t g, uint8_t b)
   {                            // Digits
      uint8_t p = N;
      void skip (uint8_t w)
      {
         for (int x = 0; x < w; x++)
            for (int y = 0; y < 7; y++)
               led_strip_set_pixel (strip, p++, 0, 0, 0);
      }
      void add (uint8_t d)
      {
         for (int x = 0; x < 5; x++)
            for (int y = 0; y < 7; y++)
               if (digit[d][y] & (0x80 >> x))
                  led_strip_set_pixel (strip, p++, r, g, b);
               else
                  led_strip_set_pixel (strip, p++, 0, 0, 0);
      }
      if (d < 0)
         skip (11);
      else if (d >= 10)
      {
         add (d / 10);
         skip (1);
         add (d % 10);
      } else
      {
         skip (3);
         add (d);
         skip (3);
      }
   }

   void show (uint8_t n)
   {
      uint8_t r = 0,
         g = 0,
         b = 0;
      if (!n)
         g = 255;               // 0
      else if (n == 1)
      {
         g = 255 / 2;
         r = 255 / 2;
      }                         // 0-32
      else if (n == N - 1)
      {
         g = 255 / 2;
         b = 255 / 2;
      }                         //  26-0
      else if (n & 1)
      {
         b = 255 / 2;
         r = 255 / 2;
      }                         // red-blue
      else if (n & 2)
         r = 255;               // red
      else
         b = 255;               // blue
      for (int i = 0; i < N; i++)
         if (i == n)
            led_strip_set_pixel (strip, i, r, g, b);
         else
            led_strip_set_pixel (strip, i, 0, 0, 0);
      if (n < N && !(n & 1))
         digits (num[n / 2], r / 3, g / 3, b / 3);
      REVK_ERR_CHECK (led_strip_refresh (strip));
   }

   show (n);

   // Button release
   while (btnpress (btn1) || btnpress (btn2))
   {
      usleep (10000);
      if (btnpress (btn1) && btnpress (btn2))
         init ();
   }
   if (btnpress (charge))
      init ();

   // Run the wheel
   n %= N;
   uint16_t run = 0;
   {                            // Set target
      uint8_t target = 255;
      while (target >= N)
         target = (esp_random () & 0xFF);       // Don't to ensure no bias
      run = N * 2 + target - n;
      if (esp_random () & 1)
         run += N;
   }

   uint8_t adj = esp_random () & 7;     // Final slow adjust

   while (run-- && !revk_shutting_down (NULL))
   {
      n = (n + 1) % N;
      show (n);
#define	SLOW	12.43           // log max time us
#define	FAST	 8.52           // log min time us
      usleep (exp (SLOW - (SLOW - FAST) * (run + adj) / 303));  // 303 should be max run, 
   }

   if (n & 1)
   {                            // Rock on to digit
      usleep (200000);
      n = (n + ((esp_random () & 1) ? N - 1 : 1)) % N;
      show (n);
   }

   // Flash colour

   usleep (300000);
   for (int i = N; i < LEDS; i++)
      led_strip_set_pixel (strip, i, 0, 0, 0);
   REVK_ERR_CHECK (led_strip_refresh (strip));
   usleep (300000);
   for (int i = N; i < LEDS; i++)
      led_strip_set_pixel (strip, i, n & 2 ? 64 : 0, !n ? 63 : 0, n && !(n & 2) ? 64 : 0);      // Colour block
   REVK_ERR_CHECK (led_strip_refresh (strip));
   usleep (300000);
   for (int i = N; i < LEDS; i++)
      led_strip_set_pixel (strip, i, 0, 0, 0);
   REVK_ERR_CHECK (led_strip_refresh (strip));
   usleep (300000);
   show (n);

   if (doneinit && webcontrol)
   {
      // Web interface
      httpd_config_t config = HTTPD_DEFAULT_CONFIG ();
      // When updating the code below, make sure this is enough
      // Note that we're also 4 adding revk's web config handlers
      config.max_uri_handlers = 8;
      if (!httpd_start (&webserver, &config))
      {
         if (webcontrol >= 2)
            revk_web_settings_add (webserver);
         //register_get_uri ("/", web_root);
      }
   }

   uint8_t tried = 0;
   uint32_t up = uptime ();
   while (doneinit && (revk_shutting_down (NULL) || btnpress (charge)) && !btnpress (btn1) && !btnpress (btn2))
   {                            // Charging
      if (!up && !tried && !revk_link_down ())
      {
         tried = 1;
         revk_command ("upgrade", NULL);
      }
      int p = revk_ota_progress ();
      if (p > 0 && p <= 100)
      {
         gpio_set_level (pwr & IO_MASK, pwr & IO_INV ? 0 : 1);
         for (int i = 0; i < p * N / 100; i++)
            led_strip_set_pixel (strip, i, 63, 63, 0);
         digits (-1, 0, 0, 0);
         REVK_ERR_CHECK (led_strip_refresh (strip));
      }
      if (up && uptime () > up + 10)
      {                         // LED Off...
         up = 0;
         gpio_set_level (pwr & IO_MASK, pwr & IO_INV ? 1 : 0);
      }
      usleep (10000);           // Wait
   }

   night (1);                   // Sleep with LEDs on
}
