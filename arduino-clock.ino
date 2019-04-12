#include <Adafruit_GFX.h>
#include <Adafruit_SPITFT_Macros.h>
#include <Adafruit_SPITFT.h>
#include <gfxfont.h>
#include <Stepper.h>

#include <Wire.h>
#include "DS3231.h"
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <MCUFRIEND_kbv.h>   // Hardware-specific library
#include <SPI.h>
#include <SdFat.h>
#include <avr/dtostrf.h>

#define BMPIMAGEOFFSET 54
#define BUFFPIXEL      20
#define USE_SDFAT 
#define SD_CS 10
#define NAMEMATCH ""
#define PALETTEDEPTH 8
#define BLACK   0x0000
#define RED     0xF800
#define GREEN   0x07E0
#define SEAFOAM 0x4E4E
#define PERIWINKLE 0x5F1F
#define WHITE   0xFFFF
#define GREY    0x8410
#define DARK_GREY 0x6B6D

enum display{
  normal = 0,
  time_edit = 1,
  picture = 2,
  ampm = 3,

};

enum button{
    mode = 1,
    forward = 1<<1,
    backward =1<<2,
    none = 1<<3,
};



void set_time(byte sec, byte minu, byte hr, byte date, byte mon, byte yr, bool hr_mode);
void get_time();
void get_atmos();
void pic_show();
void progmemPrintln(const char *str);
void progmemPrint(const char *str);
//void bmpDraw(char* filename, int x, int y);

uint16_t read16(File& f);
uint32_t read32(File& f);
uint8_t showBMP(char *nm, int x, int y);


Adafruit_BMP280 bme; // I2C
DS3231 current_time;
MCUFRIEND_kbv tft;
SdFatSoftSpi<12, 11, 13> SD; //Bit-Bang on the Shield pins

char namebuf[32] = "/"; // BMP root dir
File root;
int pathlen;

// buttons for time setting
const int button_mode = 26;    // the number of the pushbutton pin
const int button_forward = 28;    // the number of the pushbutton pin
const int button_backward = 30;    // the number of the pushbutton pin
const int int_pin = 18; // interrupt pin for the RTC sqr wave

const int stepsPerRevolution = 2048;
Stepper watch_hands(stepsPerRevolution, 32, 36, 34, 38);
const float steps_second = 2048 / 60;
float step_count = 0; 
int steps = 0;
int second_counter = 0;
int pic_timer = 5;
// button states
button button_state = none;
button last_state = none;
int lastButtonState = LOW;   // the previous reading from the input pin

// button debounce 
unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 50;    // the debounce time; increase if the output flickers

display display_state = normal;
bool Century = false;
bool h12 = true;
bool PM = true;
bool TF = true;
bool spin = true;
char buff[264];
int cx;
char dark[] = "PM";
uint8_t aspect =1;
uint16_t pixel;
const char *aspectname[] = { "PORTRAIT", "LANDSCAPE", "PORTRAIT_REV", "LANDSCAPE_REV"};
const char *colorname[] = { "BLUE", "GREEN", "RED", "GRAY" };
uint16_t colormask[] = { 0x001F, 0x07E0,0x4E4E, 0xF800, 0xFFFF };
uint16_t dx, rgb, n, wid, ht, msglin;

void setup() {
  pinMode(button_mode, INPUT);
  pinMode(button_forward, INPUT);
  pinMode(button_backward, INPUT);
  pinMode(int_pin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(int_pin), time_step, FALLING);
  Serial.begin(115200);
  Serial.println(F("BMP280 test"));

  if (!bme.begin()) {  
    Serial.println("Could not find a valid BMP280 sensor, check wiring!");
    while (1);
  }
    Serial.println("gothere");
  //set_time(01,07,15,27,3,19,h12);
  uint16_t ID = tft.readID();
  if (ID == 0xD3) ID = 0x9481;
  tft.begin(ID);
  tft.setRotation(3);
  tft.setCursor(10, 10);
  wid = tft.width();
  ht = tft.height();
  dx = wid / 32;
  // for (n = 0; n < 32; n++) {
  //   rgb = n * 8;
  //   rgb = tft.color565(rgb, rgb, rgb);
  //   tft.fillRect(n * dx, 0, dx, ht, rgb & colormask[aspect]);
  // }
  tft.fillRect(0, 0, wid, ht, PERIWINKLE);
  // line test
  //tft.drawFastHLine(0, 40, wid, RED);

  watch_hands.setSpeed(10);
  current_time.enableOscillator(TF, TF, 0);

  bool good = SD.begin(SD_CS);
  if (!good) {
      Serial.print(F("cannot start SD"));
      while (1);
  }
  root = SD.open(namebuf);
  pathlen = strlen(namebuf);
  Serial.println("gothere");
}
int count = 0;

void loop() {
  int reading0 = digitalRead(button_mode);
  int reading1 = digitalRead(button_forward);
  int reading2 = digitalRead(button_backward);
  int state_test = reading0 | (reading1 << 1) | (reading2 << 2);
  int min_val, hr_val;
 
  if (spin){
    spin = false;
    step_count = (step_count - 273 > 0) ? step_count - 273 : step_count + steps_second;
    steps = (step_count < 2) ? 35 : 34;
    watch_hands.step(steps);
    switch(display_state){
      case normal:
          //Serial.println(current_time.getTemperature() * 1.8 + 32);
          get_time();
          get_atmos();
          break;
      case time_edit:
          get_time();
          break;
      case picture:
          if (pic_timer > 5){
            pic_show();
            pic_timer = 0;
          }
          else pic_timer++;
          break;
      default:
          get_time();
          get_atmos();
          break;
    }

  }

  if ((reading0 != (lastButtonState & 1))  || (reading1 != (lastButtonState & 2) >> 1) || (reading2 != (lastButtonState & 4) >> 2 )) {
    // reset the debouncing timer
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    // whatever the reading is at, it's been there for longer than the debounce
    // delay, so take it as the actual current state:

    // if the button state has changed:
    if ((reading0 != (button_state & 1)) || reading1 || reading2 ){
      button_state = button(reading0 | (reading1 << 1) | (reading2 << 2));
      
      switch(button_state){
        case mode:
          tft.fillScreen(PERIWINKLE);
          switch (display_state){
            case normal:
                display_state = time_edit;
                break;
            case time_edit:
                display_state = picture;
                pic_timer = 5;
                break;
            case picture:
                display_state = ampm;
                break;
            case ampm:
                display_state = normal;
                break;
            default:
                display_state = normal;
                break;
          }
          break;
        case forward:
          hr_val = current_time.getHour(h12, PM);
          switch(display_state){
            case time_edit:
              min_val = current_time.getMinute() + 1;
              if (min_val == 60){
                min_val = 0;
                hr_val += 1;
              } 
              current_time.setMinute(min_val);
              if (PM){
                hr_val += 12;
              } 
              current_time.setHour(hr_val);
              current_time.setClockMode(h12);
              get_time(); 
              delay(200);
              break;
            case normal:
              break;
            case ampm:
              PM = !PM;

              delay(1000);
              break;
          }
          break;
        case backward:
          switch(display_state){
            case time_edit:
              min_val = current_time.getMinute();
              if (min_val == 0){
                min_val = 59;
                hr_val = current_time.getHour(h12, PM) - 1;
                Serial.println(hr_val);
              }
              else{
                min_val -= 1;
                hr_val = current_time.getHour(h12, PM);
                h12 = true;
              }
              current_time.setMinute(min_val);
              if (PM){
                hr_val += 12;
              } 
              current_time.setHour(hr_val);
              current_time.setClockMode(h12);
              get_time(); 
              delay(200);
              break;
            case normal:
              break;
            case ampm:
              PM = !PM;
              delay(1000);
              break;
          }
          break;
        default:
          break;
      } 
    }
  }

  lastButtonState = reading0 | (reading1 << 1) | (reading2 << 2);;

  
}


//////////////////////////
// functions
//////////////////////////

void time_step() {
  spin =true;
}

void get_atmos(){
    float temp = (bme.readTemperature() * 1.8) + 32;
    float pres = bme.readPressure() / 133.322 ;
    float alt = bme.readAltitude(1025) * 3.28;
    char val[12];
    
    tft.setTextColor(0x618B, PERIWINKLE);
    tft.setTextSize(3);
    tft.setCursor(0, 85);
    dtostrf(temp,3,2,val);
    sprintf(buff, "Temp = %s *F", val);
    tft.println(buff);

    tft.setTextSize(3);
    tft.setCursor(0, 125);
    dtostrf(pres,4,2,val);
    sprintf(buff, "Pres =%smmhg", val);
    tft.println(buff);

    tft.setTextSize(3);
    tft.setCursor(0, 165);
    dtostrf(alt,4,2,val);
    sprintf(buff, "Alt = %s ft", val);
    tft.println(buff);
  
}

void get_time(){
    int second,minute,hour,date,month,year,temperature;
    second = current_time.getSecond();
    minute = current_time.getMinute();
    hour = current_time.getHour(h12, PM);
    date = current_time.getDate();
    month = current_time.getMonth(Century);
    year = current_time.getYear();
    temperature = current_time.getTemperature();
    if (PM){
      snprintf(dark,3,"PM");
    }
    else{
      snprintf(dark,3,"AM");
    }
    
    snprintf(buff, 256, "%02d/%02d/20%d \n%02d:%02d:%02d %s\n\0)", month, date, year, hour, minute, second, dark);
    Serial.println(buff);
    tft.setTextSize(4);
    tft.setTextColor(0x618B, PERIWINKLE);
    tft.setCursor(0, 10);
    tft.println(buff);    
}

void set_time(byte sec, byte minu, byte hr, byte date, byte mon, byte yr, bool hr_mode){
    current_time.setSecond(sec); 
      // In addition to setting the seconds, this clears the 
      // "Oscillator Stop Flag".
    current_time.setMinute(minu); 
      // Sets the minute
    current_time.setHour(hr); 
      // Sets the hour
    //current_time.setDoW(dow); 
      // Sets the Day of the Week (1-7);
    current_time.setDate(date); 
      // Sets the Date of the Month
    current_time.setMonth(mon); 
      // Sets the Month of the year
    current_time.setYear(yr); 
      // Last two digits of the year
    current_time.setClockMode(hr_mode); 
  }

void pic_show(){
    char *nm = namebuf + pathlen;
    File f = root.openNextFile();
    uint8_t ret;
    uint32_t start;
    if (f != NULL) {
        f.getName(nm, 32 - pathlen);
        f.close();
        strlwr(nm);
        if (strstr(nm, ".bmp") != NULL && strstr(nm, NAMEMATCH) != NULL) {
            Serial.print(namebuf);
            Serial.print(F(" - "));
            tft.fillScreen(0);
            start = millis();
            ret = showBMP(namebuf, 5, 5);
            switch (ret) {
                case 0:
                    Serial.print(millis() - start);
                    Serial.println(F("ms"));
                    break;
                case 1:
                    Serial.println(F("bad position"));
                    break;
                case 2:
                    Serial.println(F("bad BMP ID"));
                    break;
                case 3:
                    Serial.println(F("wrong number of planes"));
                    break;
                case 4:
                    Serial.println(F("unsupported BMP format"));
                    break;
                case 5:
                    Serial.println(F("unsupported palette"));
                    break;
                default:
                    Serial.println(F("unknown"));
                    break;
            }
        }
    }
    else root.rewindDirectory();
}

uint16_t read16(File& f) {
    uint16_t result;         // read little-endian
    f.read(&result, sizeof(result));
    return result;
}

uint32_t read32(File& f) {
    uint32_t result;
    f.read(&result, sizeof(result));
    return result;
}

// // These read 16- and 32-bit types from the SD card file.
// // BMP data is stored little-endian, Arduino is little-endian too.
// // May need to reverse subscript order if porting elsewhere.

// uint16_t read16(SdFile& f) {
// 	uint16_t result;
// 	((uint8_t *)&result)[0] = f.read(); // LSB
// 	((uint8_t *)&result)[1] = f.read(); // MSB
// 	return result;
// }

// uint32_t read32(SdFile& f) {
// 	uint32_t result;
// 	((uint8_t *)&result)[0] = f.read(); // LSB
// 	((uint8_t *)&result)[1] = f.read();
// 	((uint8_t *)&result)[2] = f.read();
// 	((uint8_t *)&result)[3] = f.read(); // MSB
// 	return result;
// }

uint8_t showBMP(char *nm, int x, int y)
{
    File bmpFile;
    int bmpWidth, bmpHeight;    // W+H in pixels
    uint8_t bmpDepth;           // Bit depth (currently must be 24, 16, 8, 4, 1)
    uint32_t bmpImageoffset;    // Start of image data in file
    uint32_t rowSize;           // Not always = bmpWidth; may have padding
    uint8_t sdbuffer[3 * BUFFPIXEL];    // pixel in buffer (R+G+B per pixel)
    uint16_t lcdbuffer[(1 << PALETTEDEPTH) + BUFFPIXEL], *palette = NULL;
    uint8_t bitmask, bitshift;
    boolean flip = true;        // BMP is stored bottom-to-top
    int w, h, row, col, lcdbufsiz = (1 << PALETTEDEPTH) + BUFFPIXEL, buffidx;
    uint32_t pos;               // seek position
    boolean is565 = false;      //

    uint16_t bmpID;
    uint16_t n;                 // blocks read
    uint8_t ret;

    if ((x >= tft.width()) || (y >= tft.height()))
        return 1;               // off screen

    bmpFile = SD.open(nm);      // Parse BMP header
    bmpID = read16(bmpFile);    // BMP signature
    (void) read32(bmpFile);     // Read & ignore file size
    (void) read32(bmpFile);     // Read & ignore creator bytes
    bmpImageoffset = read32(bmpFile);       // Start of image data
    (void) read32(bmpFile);     // Read & ignore DIB header size
    bmpWidth = read32(bmpFile);
    bmpHeight = read32(bmpFile);
    n = read16(bmpFile);        // # planes -- must be '1'
    bmpDepth = read16(bmpFile); // bits per pixel
    pos = read32(bmpFile);      // format
    if (bmpID != 0x4D42) ret = 2; // bad ID
    else if (n != 1) ret = 3;   // too many planes
    else if (pos != 0 && pos != 3) ret = 4; // format: 0 = uncompressed, 3 = 565
    else if (bmpDepth < 16 && bmpDepth > PALETTEDEPTH) ret = 5; // palette 
    else {
        bool first = true;
        is565 = (pos == 3);               // ?already in 16-bit format
        // BMP rows are padded (if needed) to 4-byte boundary
        rowSize = (bmpWidth * bmpDepth / 8 + 3) & ~3;
        if (bmpHeight < 0) {              // If negative, image is in top-down order.
            bmpHeight = -bmpHeight;
            flip = false;
        }

        w = bmpWidth;
        h = bmpHeight;
        if ((x + w) >= tft.width())       // Crop area to be loaded
            w = tft.width() - x;
        if ((y + h) >= tft.height())      //
            h = tft.height() - y;

        if (bmpDepth <= PALETTEDEPTH) {   // these modes have separate palette
            bmpFile.seek(BMPIMAGEOFFSET); //palette is always @ 54
            bitmask = 0xFF;
            if (bmpDepth < 8)
                bitmask >>= bmpDepth;
            bitshift = 8 - bmpDepth;
            n = 1 << bmpDepth;
            lcdbufsiz -= n;
            palette = lcdbuffer + lcdbufsiz;
            for (col = 0; col < n; col++) {
                pos = read32(bmpFile);    //map palette to 5-6-5
                palette[col] = ((pos & 0x0000F8) >> 3) | ((pos & 0x00FC00) >> 5) | ((pos & 0xF80000) >> 8);
            }
        }

        // Set TFT address window to clipped image bounds
        tft.setAddrWindow(x, y, x + w - 1, y + h - 1);
        for (row = 0; row < h; row++) { // For each scanline...
            // Seek to start of scan line.  It might seem labor-
            // intensive to be doing this on every line, but this
            // method covers a lot of gritty details like cropping
            // and scanline padding.  Also, the seek only takes
            // place if the file position actually needs to change
            // (avoids a lot of cluster math in SD library).
            uint8_t r, g, b, *sdptr;
            int lcdidx, lcdleft;
            if (flip)   // Bitmap is stored bottom-to-top order (normal BMP)
                pos = bmpImageoffset + (bmpHeight - 1 - row) * rowSize;
            else        // Bitmap is stored top-to-bottom
                pos = bmpImageoffset + row * rowSize;
            if (bmpFile.position() != pos) { // Need seek?
                bmpFile.seek(pos);
                buffidx = sizeof(sdbuffer); // Force buffer reload
            }

            for (col = 0; col < w; ) {  //pixels in row
                lcdleft = w - col;
                if (lcdleft > lcdbufsiz) lcdleft = lcdbufsiz;
                for (lcdidx = 0; lcdidx < lcdleft; lcdidx++) { // buffer at a time
                    uint16_t color;
                    // Time to read more pixel data?
                    if (buffidx >= sizeof(sdbuffer)) { // Indeed
                        bmpFile.read(sdbuffer, sizeof(sdbuffer));
                        buffidx = 0; // Set index to beginning
                        r = 0;
                    }
                    switch (bmpDepth) {          // Convert pixel from BMP to TFT format
                        case 24:
                            b = sdbuffer[buffidx++];
                            g = sdbuffer[buffidx++];
                            r = sdbuffer[buffidx++];
                            color = tft.color565(r, g, b);
                            break;
                        case 16:
                            b = sdbuffer[buffidx++];
                            r = sdbuffer[buffidx++];
                            if (is565)
                                color = (r << 8) | (b);
                            else
                                color = (r << 9) | ((b & 0xE0) << 1) | (b & 0x1F);
                            break;
                        case 1:
                        case 4:
                        case 8:
                            if (r == 0)
                                b = sdbuffer[buffidx++], r = 8;
                            color = palette[(b >> bitshift) & bitmask];
                            r -= bmpDepth;
                            b <<= bmpDepth;
                            break;
                    }
                    lcdbuffer[lcdidx] = color;

                }
                tft.pushColors(lcdbuffer, lcdidx, first);
                first = false;
                col += lcdidx;
            }           // end cols
        }               // end rows
        tft.setAddrWindow(0, 0, tft.width() - 1, tft.height() - 1); //restore full screen
        ret = 0;        // good render
    }
    bmpFile.close();
    return (ret);
}

// void bmpDraw(char* filename, int x, int y) {

// 	SdFile   bmpFile;
// 	int      bmpWidth, bmpHeight;   // W+H in pixels
// 	uint8_t  bmpDepth;              // Bit depth (currently must be 24)
// 	uint8_t	 headerSize;
// 	uint32_t bmpImageoffset;        // Start of image data in file
// 	uint32_t rowSize;     // Not always = bmpWidth; may have padding
// 	uint32_t fileSize;
// 	boolean  goodBmp = false;       // Set to true on valid header parse
// 	boolean  flip = true;        // BMP is stored bottom-to-top
// 	uint16_t w, h, row, col;
// 	uint8_t  r, g, b;
// 	uint32_t pos = 0, startTime;

// 	if ((x >= tft.width()) || (y >= tft.height())) return;

// 	progmemPrint(PSTR("Loading image '"));
// 	Serial.print(filename);
// 	Serial.println('\'');
// 	startTime = millis();
// 	// Open requested file on SD card
// 	if (!bmpFile.open(filename, O_READ)) {
// 		Serial.println("File open failed.");
// 		return;
// 	}
// 	else {
// 		//Serial.println("File opened.");
// 	}

// 	// Parse BMP header
// 	if (read16(bmpFile) == 0x4D42) { // BMP signature
// 		fileSize = read32(bmpFile);
// 		//progmemPrint(PSTR("File size: ")); Serial.println(fileSize);
// 		(void)read32(bmpFile); // Read & ignore creator bytes
// 		bmpImageoffset = read32(bmpFile); // Start of image data
// 		//progmemPrint(PSTR("Image Offset: ")); Serial.println(bmpImageoffset, DEC);
// 		// Read DIB header
// 		headerSize = read32(bmpFile);
// 		//progmemPrint(PSTR("Header size: ")); Serial.println(headerSize);
// 		bmpWidth = read32(bmpFile);
// 		bmpHeight = read32(bmpFile);
// 		if (read16(bmpFile) == 1) { // # planes -- must be '1'
// 			bmpDepth = read16(bmpFile); // bits per pixel
// 			//progmemPrint(PSTR("Bit Depth: ")); Serial.println(bmpDepth);
// 			if (read32(bmpFile) == 0) // 0 = uncompressed
// 			{
// 				//progmemPrint(PSTR("Image size: "));
// 				//Serial.print(bmpWidth);
// 				//Serial.print('x');
// 				//Serial.println(bmpHeight);

// 				// If bmpHeight is negative, image is in top-down order.
// 				// This is not canon but has been observed in the wild.
// 				if (bmpHeight < 0) {
// 					bmpHeight = -bmpHeight;
// 					flip = false;
// 				}

// 				// Crop area to be loaded
// 				w = bmpWidth;
// 				h = bmpHeight;
// 				if ((x + w - 1) >= tft.width())  w = tft.width() - x;
// 				if ((y + h - 1) >= tft.height()) h = tft.height() - y;
			
// 				// Set TFT address window to clipped image bounds
// 				tft.setAddrWindow(x, y, x + w - 1, y + h - 1);

// 				if (bmpDepth == 16)	//565 format
// 				{
// 					goodBmp = true; // Supported BMP format -- proceed!

// 					uint16_t buffer[BUFFPIXELCOUNT]; // pixel buffer

// 					bmpFile.seekSet(54);	//skip header
// 					uint32_t totalPixels = (uint32_t)bmpWidth*(uint32_t)bmpHeight;
// 					uint16_t numFullBufferRuns = totalPixels / BUFFPIXELCOUNT;
// 					for (uint32_t p = 0; p < numFullBufferRuns; p++) {
// 						// read pixels into the buffer
// 						bmpFile.read(buffer, 2 * BUFFPIXELCOUNT);
// 						// push them to the diplay
// 						tft.pushColors(buffer, 0, BUFFPIXELCOUNT);
						
// 					}

// 					// render any remaining pixels that did not fully fit the buffer
// 					uint32_t remainingPixels = totalPixels % BUFFPIXELCOUNT;
// 					if (remainingPixels > 0)
// 					{
// 						bmpFile.read(buffer, 2 * remainingPixels);
// 						tft.pushColors(buffer, 0, remainingPixels);
// 					}

// 				}
// 				//else if (bmpDepth == 24)	// standard 24bit bmp
// 				//{
// 				//	goodBmp = true; // Supported BMP format -- proceed!
// 				//	uint16_t bufferSize = min(w, BUFFPIXELCOUNT);
// 				//	uint8_t  sdbuffer[3 * bufferSize]; // pixel in buffer (R+G+B per pixel)
// 				//	uint16_t lcdbuffer[bufferSize];  // pixel out buffer (16-bit per pixel)

// 				//	// BMP rows are padded (if needed) to 4-byte boundary
// 				//	rowSize = (bmpWidth * 3 + 3) & ~3;

// 				//	for (row = 0; row < h; row++) { // For each scanline...
// 				//		// Seek to start of scan line.  It might seem labor-
// 				//		// intensive to be doing this on every line, but this
// 				//		// method covers a lot of gritty details like cropping
// 				//		// and scanline padding.  Also, the seek only takes
// 				//		// place if the file position actually needs to change
// 				//		// (avoids a lot of cluster math in SD library).

// 				//		if (flip) // Bitmap is stored bottom-to-top order (normal BMP)
// 				//			pos = bmpImageoffset + (bmpHeight - 1 - row) * rowSize;
// 				//		else     // Bitmap is stored top-to-bottom
// 				//			pos = bmpImageoffset + row * rowSize;
// 				//		if (bmpFile.curPosition() != pos) { // Need seek?
// 				//			bmpFile.seekSet(pos);
// 				//		}

// 				//		for (col = 0; col < w; col += bufferSize)
// 				//		{
// 				//			// read pixels into the buffer
// 				//			bmpFile.read(sdbuffer, 3 * bufferSize);

// 				//			// convert color
// 				//			for (int p = 0; p < bufferSize; p++)
// 				//			{
// 				//				b = sdbuffer[3 * p];
// 				//				g = sdbuffer[3 * p + 1];
// 				//				r = sdbuffer[3 * p + 2];
// 				//				lcdbuffer[p] = tft.color565(r, g, b);
// 				//			}
// 				//			// push buffer to TFT
// 				//			tft.pushColors(lcdbuffer, 0, bufferSize);
// 				//		}

// 				//		// render any remaining pixels that did not fully fit the buffer
// 				//		uint16_t remainingPixels = w % bufferSize;
// 				//		if (remainingPixels > 0)
// 				//		{
// 				//			bmpFile.read(sdbuffer, 3 * remainingPixels);

// 				//			for (int p = 0; p < remainingPixels; p++)
// 				//			{
// 				//				b = sdbuffer[3 * p];
// 				//				g = sdbuffer[3 * p + 1];
// 				//				r = sdbuffer[3 * p + 2];
// 				//				lcdbuffer[p] = tft.color565(r, g, b);
// 				//			}

// 				//			tft.pushColors(lcdbuffer, 0, remainingPixels);
// 				//		}
// 				//	}
// 				//}
// 				else
// 				{
// 					progmemPrint(PSTR("Unsupported Bit Depth."));
// 				}

// 				if (goodBmp)
// 				{
// 					progmemPrint(PSTR("Loaded in "));
// 					Serial.print(millis() - startTime);
// 					Serial.println(" ms");
// 				}
// 			}
// 		}
// 	}

// 	bmpFile.close();
// 	if (!goodBmp) progmemPrintln(PSTR("BMP format not recognized."));
// }


// Copy string from flash to serial port
// Source string MUST be inside a PSTR() declaration!
void progmemPrint(const char *str) {
	char c;
	while (c = pgm_read_byte(str++)) Serial.print(c);
}

// Same as above, with trailing newline
void progmemPrintln(const char *str) {
	progmemPrint(str);
	Serial.println();
}
