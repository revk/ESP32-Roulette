/* Roulette app */
/* Copyright ©2019 - 23 Adrian Kennard, Andrews & Arnold Ltd.See LICENCE file for details .GPL 3.0 */

static __attribute__((unused))
     const char TAG[] = "Roulette";

#include "revk.h"
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include <driver/gpio.h>
#include <driver/uart.h>
#include "led_strip.h"
#include <esp_http_server.h>
#include <math.h>

     static httpd_handle_t webserver = NULL;

#define	BTN1	4
#define	BTN2	5
#define	PWR	6
#define	RGB	7
#define	LEDS	151

RTC_NOINIT_ATTR uint8_t	n;	// Current number 0-(n-1) - remembered between runs
				
const uint8_t num[]={0,32,15,19,4,21,2,25,17,34,6,37,13,36,11,30,8,23,10,5,24,16,33,1,20,14,31,9,22,18,29,7,28,12,35,3,26};
#define	N	(2*sizeof(num)/sizeof(*num))

const uint8_t digit[][7]={
   {0x20, 0x50, 0x88, 0x88, 0x88, 0x50, 0x20},
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
#define io(n,d)           uint8_t n;
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
     const char *app_callback (int client, const char *prefix, const char *target, const char *suffix, jo_t j)
{
   if (client || !prefix || target || strcmp (prefix, prefixcommand))
      return NULL;              // Not for us or not a command from main MQTT
   return "Not known";
}

void
app_main ()
{
#ifdef  CONFIG_IDF_TARGET_ESP32S3
   {                            // All unused input pins pull down
      gpio_config_t c = {.pull_down_en = 1,.mode = GPIO_MODE_DISABLE };
      for (uint8_t p = 0; p <= 48; p++)
         if (gpio_ok (p) & 2)
            c.pin_bit_mask |= (1LL << p);
      gpio_config (&c);
   }
#endif

   gpio_reset_pin (PWR);
   gpio_set_level (PWR, 0);
   gpio_set_direction (PWR, GPIO_MODE_OUTPUT);
   usleep (10000);

   led_strip_handle_t strip = NULL;
   led_strip_config_t strip_config = {
      .strip_gpio_num = RGB,
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

   void show(uint8_t n)
   {
	   uint8_t r=0,g=0,b=0;
	   if(n==1){g=127;r=127;} // 0-32
	   else if(n==N-2){g=127;b=127;} //  26-0
	   else if(n&1){b=127;r=127;} // red-blue
	   else if(n&2)r=255;
	   else b=255;
	   for(int i=0;i<N;i++)led_strip_set_pixel(strip,i,r,g,b);
	   if(n&1)for(int i=N;i<LEDS;i++)led_strip_set_pixel(strip,i,0,0,0); // Not on number;
	   else
	   { // Digits

	   }
   REVK_ERR_CHECK (led_strip_refresh (strip));
   }

   // Checking keys


   // Run the wheel
n%=N;
uint8_t run=0;
{ // Set target
uint8_t target=255;
while(target>=N)target=(esp_random()&0xFF); // Don't to ensure no bias
					   run=N*4+target-n; 
					    }

while(run--)
{
	n=(n+1)%N;
	show(n);
	usleep(100000);
}

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

   if (webcontrol)
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

   while (1)
      sleep (1);
}
