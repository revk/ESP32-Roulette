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
     RTC_NOINIT_ATTR uint8_t override;  // Override next number

     const uint8_t num[] =
        { 0, 32, 15, 19, 4, 21, 2, 25, 17, 34, 6, 27, 13, 36, 11, 30, 8, 23, 10, 5, 24, 16, 33, 1, 20, 14, 31, 9, 22, 18, 29, 7, 28,
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
   {0x80, 0x80, 0xB8, 0x88, 0xB8, 0x20, 0x38},  // ½
};

const uint16_t icon[][7] = {
   {0xAE00, 0xA200, 0xAEA0, 0xA2A0, 0xAEE0, 0xA020, 0xA020},    // 11¾
};

uint8_t overridenext = 0;

const char *
app_callback (int client, const char *prefix, const char *target, const char *suffix, jo_t j)
{
   if (client || !prefix || target || strcmp (prefix, prefixcommand))
      return NULL;              // Not for us or not a command from main MQTT
   if (suffix && *suffix >= '0' && *suffix <= '9')
   {
      overridenext = atoi (suffix) ? : 100;
      return "";
   }
   return NULL;
}

static uint8_t doneinit = 0;

void
night (uint8_t p)
{
   if (doneinit)
      revk_pre_shutdown ();
   if (pwr.set && rtc_gpio_is_valid_gpio (pwr.num))
   {                            // LED power
      gpio_set_level (pwr.num, (p ? 1 : 0) ^ (pwr.invert ? 1 : 0));
      rtc_gpio_set_direction_in_sleep (pwr.num, RTC_GPIO_MODE_OUTPUT_ONLY);
      rtc_gpio_set_level (pwr.num, (p ? 1 : 0) ^ (pwr.invert ? 1 : 0));
      rtc_gpio_hold_dis (pwr.num);
      rtc_gpio_isolate (pwr.num);
      rtc_gpio_hold_en (pwr.num);
   }
   if (btn1.set && rtc_gpio_is_valid_gpio (btn1.num))
   {                            // Button 1
      rtc_gpio_set_direction_in_sleep (btn1.num, RTC_GPIO_MODE_INPUT_ONLY);
      rtc_gpio_pullup_dis (btn1.num);
      rtc_gpio_pulldown_dis (btn1.num);
      rtc_gpio_isolate (btn1.num);
   }
   if (btn2.set && rtc_gpio_is_valid_gpio (btn2.num))
   {                            // Button 2
      rtc_gpio_set_direction_in_sleep (btn2.num, RTC_GPIO_MODE_INPUT_ONLY);
      rtc_gpio_pullup_dis (btn2.num);
      rtc_gpio_pulldown_dis (btn2.num);
      rtc_gpio_isolate (btn2.num);
   }
   if (charge.set && rtc_gpio_is_valid_gpio (charge.num))
   {                            // Charge
      rtc_gpio_set_direction_in_sleep (charge.num, RTC_GPIO_MODE_INPUT_ONLY);
      rtc_gpio_pullup_dis (charge.num);
      rtc_gpio_pulldown_dis (charge.num);
      rtc_gpio_isolate (charge.num);
   }
   // TODO work out "change of state" wake, to include charge
   uint64_t mask = 0;
   if ((btn1.invert) && rtc_gpio_is_valid_gpio (btn1.num))
      mask |= (1LL << (btn1.num));
   else if ((btn2.invert) && rtc_gpio_is_valid_gpio (btn2.num))
      mask |= (1LL << (btn2.num));
   if (mask)
      esp_sleep_enable_ext1_wakeup (mask, ESP_EXT1_WAKEUP_ALL_LOW);     // Active low, so one button, annoying
   else
   {
      if (btn1.set && rtc_gpio_is_valid_gpio (btn1.num))
         mask |= (1LL << (btn1.num));
      if (btn2.set && rtc_gpio_is_valid_gpio (btn2.num))
         mask |= (1LL << (btn2.num));
      esp_sleep_enable_ext1_wakeup (mask, ESP_EXT1_WAKEUP_ANY_HIGH);    // Active high, so any button
   }
   // Go to sleep
   //ESP_LOGE (TAG, "LED power %d (p=%d)", (p ? 1 : 0) ^ (pwr.invert ? 1 : 0), p);
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
   revk_start ();
}

void
app_main ()
{
   esp_reset_reason_t reset = esp_reset_reason ();

   if (!pwr.set || !rgb.set || reset == ESP_RST_POWERON || reset == ESP_RST_EXT || reset == ESP_RST_BROWNOUT || reset == ESP_RST_PANIC)
   {
      override = 0;
      n = esp_random () % N;
      init ();                  // Get values
   }

   revk_gpio_input(btn1);
   revk_gpio_input(btn2);
   revk_gpio_input(charge);
   revk_gpio_output(pwr);

   //ESP_LOGE (TAG, "Ext1 %llX Reset %d BTN1 %X BTN2 %X PWR %X RGB %X Press1 %d Press2 %d", esp_sleep_get_ext1_wakeup_status (), reset, btn1, btn2, pwr, rgb, revk_gpio_get (btn1), revk_gpio_get (btn2));

   if (!doneinit && !revk_gpio_get (btn1) && !revk_gpio_get (btn2) && !revk_gpio_get (charge) && !esp_sleep_get_ext1_wakeup_status ())
      night (0);                // Off, and sleep

   {                            // All unused input pins pull down
      gpio_config_t c = {.pull_down_en = 1,.mode = GPIO_MODE_DISABLE };
      for (uint8_t p = 0; p <= 48; p++)
         if (gpio_ok (p) == 3)
            c.pin_bit_mask |= (1LL << p);
      if (btn1.set)
         c.pin_bit_mask &= ~(1LL << btn1.num);
      if (btn2.set)
         c.pin_bit_mask &= ~(1LL << btn2.num);
      if (charge.set)
         c.pin_bit_mask &= ~(1LL << charge.num);
      if (pwr.set)
         c.pin_bit_mask &= ~(1LL << pwr.num);
      if (rgb.set)
         c.pin_bit_mask &= ~(1LL << rgb.num);
      gpio_config (&c);
   }

   led_strip_handle_t strip = NULL;
   led_strip_config_t strip_config = {
      .strip_gpio_num = rgb.num ,
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
      void addi (uint8_t d)
      {
         for (int x = 0; x < 11; x++)
            for (int y = 0; y < 7; y++)
               if (icon[d][y] & (0x8000 >> x))
                  led_strip_set_pixel (strip, p++, r, g, b);
               else
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
      else if (d == 113)
         addi (0);
      else if (d > 100 && d < 200 && (d % 10) == 5)
      {
         if (d / 10 % 10)
         {
            add (d / 10 % 10);
            skip (1);
            add (10);
         } else
         {
            skip (3);
            add (10);
            skip (3);
         }
      } else if (d >= 10)
      {
         add (d / 10 % 10);
         skip (1);
         add (d % 10);
      } else
      {
         skip (3);
         add (d);
         skip (3);
      }
   }

   void show (uint8_t n, int d)
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
      if (d >= 0)
         digits (d, r / 3, g / 3, b / 3);
      REVK_ERR_CHECK (led_strip_refresh (strip));
   }

   show (n, (n & 1) ? -1 : num[n / 2]);

   // Button release
   while (revk_gpio_get (btn1) || revk_gpio_get (btn2))
   {
      usleep (10000);
      if (revk_gpio_get (btn1) && revk_gpio_get (btn2))
         init ();
   }
   if (revk_gpio_get (charge) || override)
      init ();

   // Run the wheel
   n %= N;
   uint16_t run = 0;
   {                            // Set target
      uint8_t target = 255;
      if (override)
      {                         // Preset target
         ESP_LOGE (TAG, "Override %d", override);
         target = 0;
         int i = 0;
         if (override == 113)
            target = 14 * 2 + 1;        // 11¾ so land on 11++ (which is 14th number)
         else if (override > 100 && override < 200 && (override % 10) == 5)
         {
            for (i = 0; i < sizeof (num) && num[i] != (override / 10 % 10); i++);
            if (i < sizeof (num))
               target = i * 2 + 1;      // N 1/2
         } else
         {
            if (override == 100)
               override = 0;
            for (i = 0; i < sizeof (num) && num[i] != override; i++);
            if (i < sizeof (num))
               target = i * 2;
         }
      } else
      {                         // Random target
         while (target >= N)
            target = (esp_random () & 0xFF);    // Don't to ensure no bias
      }
      run = N * 2 + target - n;
      if (target < n)
         run += N;
      if (esp_random () & 1)
         run += N;
      ESP_LOGE (TAG, "Target %d(%d) run %d", target, num[target / 2], run);
   }

   uint8_t adj = esp_random () & 7;     // Final slow adjust

   while (run-- && (!doneinit || !revk_shutting_down (NULL)) && !revk_gpio_get (btn2))
   {
      n = (n + 1) % N;
      if (!run && override)
         show (n, override);
      else
         show (n, (n & 1) ? -1 : num[n / 2]);
#define	SLOW	12.43           // log max time us
#define	FAST	 8.52           // log min time us
      usleep (exp (SLOW - (SLOW - FAST) * (run + adj) / 303));  // 303 should be max run, 
   }
   if (revk_gpio_get (btn2))
      init ();

   if (!override && (n & 1))
   {                            // Rock on to digit
      usleep (200000);
      n = (n + ((esp_random () & 1) ? N - 1 : 1)) % N;
      show (n, (n & 1) ? -1 : num[n / 2]);
   }

   // Flash colour

   usleep (300000);
   for (int i = N; i < LEDS; i++)
      led_strip_set_pixel (strip, i, 0, 0, 0);  // Off
   REVK_ERR_CHECK (led_strip_refresh (strip));
   usleep (300000);
   for (int i = N; i < LEDS; i++)
      led_strip_set_pixel (strip, i, n & 2 ? 64 : 0, !n ? 63 : 0, n && !(n & 2) ? 64 : 0);      // Colour block
   REVK_ERR_CHECK (led_strip_refresh (strip));
   usleep (300000);
   for (int i = N; i < LEDS; i++)
      led_strip_set_pixel (strip, i, 0, 0, 0);  // Off
   REVK_ERR_CHECK (led_strip_refresh (strip));
   usleep (300000);
   show (n, override ? : num[n / 2]);   // Number

   override = overridenext;     // Clear

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
   while (doneinit && (up || revk_shutting_down (NULL) || revk_gpio_get (charge) || revk_gpio_get (btn2)) && !revk_gpio_get (btn1))
   {                            // Charging
      if (!tried && !revk_link_down ())
      {
         ESP_LOGE (TAG, "Auto upgrade");
         tried = 1;
         revk_command ("upgrade", NULL);
      }
      int p = revk_ota_progress ();
      if (p > 0 && p <= 100)
      {
	      revk_gpio_set(pwr,1);
         for (int i = 0; i < p * N / 100; i++)
            led_strip_set_pixel (strip, i, 63, 63, 0);
         digits (-1, 0, 0, 0);
         REVK_ERR_CHECK (led_strip_refresh (strip));
      }
      if (up && uptime () > up + 10)
      {                         // LED Off...
         up = 0;
	                 revk_gpio_set(pwr,0);
      }
      usleep (10000);           // Wait
   }

   night (1);                   // Sleep with LEDs on
}
