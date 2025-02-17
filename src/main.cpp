/* ESP32 +TFT + Muses72323 Volume Controller
*********************

Author Geoff Webster

Initial version 1.0 July 2024
- SetVolume routine to display atten/gain (-111.75dB min to 0dB)

*/

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <Preferences.h>
#include <SPI.h>
#include <TFT_eSPI.h> // Hardware-specific library
#include <RC5.h>
#include <Muses72323.h> // Hardware-specific library
#include <ESP32RotaryEncoder.h>
#include <MCP23S08.h>   // Hardware-specific library
#include "Free_Fonts.h" // Include the Free fonts header file

// Current software
#define softTitle1 "ESP32/TFT/Muses"
#define softTitle2 "+ Clock Controller"
// version number
#define VERSION_NUM "1.0"

/******* MACHINE STATES *******/
#define STATE_RUN 0     // normal run state
#define STATE_IO 1      // when user selects input/output
#define ON LOW
#define OFF HIGH
#define STANDBY 0 // Standby
#define ACTIVE 1  // Active

// Preference modes
#define RW_MODE false
#define RO_MODE true

#define TIME_EXITSELECT 5 //** Time in seconds to exit I/O select mode when no activity

Preferences preferences;

// WiFi network connect settings
// Replace with your network credentials
// const char *ssid = "REPLACE_WITH_YOUR_SSID";
// const char *password = "REPLACE_WITH_YOUR_PASSWORD";
const char *ssid = "PLUSNET-9FC9NQ";
const char *password = "M93ucVcxRGCKeR";

// TimeServer settings
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;        // because you're in Portugal, time offset is same as UK
const int daylightOffset_sec = 3600; // provides offset of one hour when in Daylight Saving Time

// 23S08 Construct
MCP23S08 MCP(10); //  HW SPI address 0x00, CS GPIO10

TFT_eSPI tft = TFT_eSPI(); // Invoke custom library

// define IR input
unsigned int IR_PIN = 27;
// RC5 construct
RC5 rc5(IR_PIN);

// define preAmp control pins
const int s_select_72323 = 16;
//  The address wired into the muses chip (usually 0).
static const byte MUSES_ADDRESS = 0;

// preAmp construct
static Muses72323 Muses(MUSES_ADDRESS, s_select_72323); // muses chip address (usually 0), slave select pin0;

// define encoder pins
const uint8_t DI_ENCODER_A = 33;
const uint8_t DI_ENCODER_B = 32;
const int8_t DI_ENCODER_SW = 12;

// Rotary construct
RotaryEncoder rotaryEncoder(DI_ENCODER_A, DI_ENCODER_B, DI_ENCODER_SW);

/******* TIMING *******/
unsigned long milOnButton; // Stores last time for switch press
unsigned long milOnAction; // Stores last time of user input

/********* Global Variables *******************/

float atten;    // current attenuation, between 0 and -111.75
int16_t volume; // current volume, between 0 and -447
bool backlight; // current backlight state
uint16_t counter = 0;
uint8_t source;        // current input channel
uint8_t oldsource = 1; // previous input channel
bool isMuted;          // current mute status
uint8_t state = 0;     // current machine state
uint8_t balanceState;  // current balance state
bool btnstate = 0;
bool oldbtnstate = 0;
int clockdelay;
char buffer2[20] = "";
struct tm timeinfo;

/*System addresses and codes used here match RC-5 infra-red codes for amplifiers (and CDs)*/
uint16_t oldtoggle;
u_char oldaddress;
u_char oldcommand;
u_char toggle;
u_char address;
u_char command;

// Used to know when to fire an event when the knob is turned
volatile bool turnedRightFlag = false;
volatile bool turnedLeftFlag = false;

char buffer1[20] = "";

// Global Constants
//------------------
const char *inputName[] = {"  Phono ", "   Media  ", "     CD    ", "   Tuner  "}; // Elektor i/p board

// Function prototypes
void RC5Update(void);
void setIO();
void knobCallback(long value);
void buttonCallback(unsigned long duration);
void RotaryUpdate();
void volumeUpdate();
void setVolume();
void sourceUpdate();
void mute();
void unMute();
void toggleMute();
void setTimezone(String timezone);
void initTime(String timezone);
void displayLocalTime();

void setTimezone(String timezone){
  Serial.printf("  Setting Timezone to %s\n",timezone.c_str());
  setenv("TZ",timezone.c_str(),1);  //  Now adjust the TZ.  Clock settings are adjusted to show the new local time
  tzset();
}

void initTime(String timezone){
  struct tm timeinfo;

  Serial.println("Setting up time");
  configTime(0, 0, "pool.ntp.org");    // First connect to NTP server, with 0 TZ offset
  if(!getLocalTime(&timeinfo)){
    Serial.println("  Failed to obtain time");
    return;
  }
  Serial.println("  Got the time from NTP");
  // Now we can set the real timezone
  setTimezone(timezone);
}

void displayLocalTime()
{
  if (!getLocalTime(&timeinfo))
  {
    tft.drawString("Failed to obtain time", 160, 40, 1);
    return;
  }
  if (clockdelay != timeinfo.tm_sec)
  {
    sprintf(buffer2, "   %02d:%02d:%02d   ", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    tft.drawString(buffer2, 160, 40, 1);
    clockdelay = timeinfo.tm_sec;
  }
}

void knobCallback(long value)
{
  // See the note in the `loop()` function for
  // an explanation as to why we're setting
  // boolean values here instead of running
  // functions directly.

  // Don't do anything if either flag is set;
  // it means we haven't taken action yet
  if (turnedRightFlag || turnedLeftFlag)
    return;

  // Set a flag that we can look for in `loop()`
  // so that we know we have something to do
  switch (value)
  {
  case 1:
    turnedRightFlag = true;
    break;

  case -1:
    turnedLeftFlag = true;
    break;
  }

  // Override the tracked value back to 0 so that
  // we can continue tracking right/left events
  rotaryEncoder.setEncoderValue(0);
}

void buttonCallback(unsigned long duration)
{
  int _duration = duration;
  if (_duration > 50)
  {
    switch (state)
    {
    case STATE_RUN:
      state = STATE_IO;
      milOnButton = millis();
      break;
    default:
      break;
    }
  }
}

void volumeUpdate()
{
  if (turnedRightFlag)
  {
    if (isMuted)
    {
      unMute();
    }
    if (volume < 0)
    {
      volume = volume + 1;
      setVolume();
    }
    // Set flag back to false so we can watch for the next move
    turnedRightFlag = false;
  }
  else if (turnedLeftFlag)
  {
    if (isMuted)
    {
      unMute();
    }
    if (volume > -447)
    {
      volume = volume - 1;
      setVolume();
    }
    // Set flag back to false so we can watch for the next move
    turnedLeftFlag = false;
  }
}

void setVolume()
{
  // For debug
  /*
  Serial.println("");
  Serial.println(volume);
  */

  // set new volume setting
  Muses.setVolume(volume, volume);
  preferences.putInt("VOLUME", volume);
  // display volume setting
  if (!backlight)
  {
    backlight = ACTIVE;
    digitalWrite(TFT_BL, HIGH); // Turn on backlight
  }
  float atten = ((float)volume / 4);
  sprintf(buffer1, "  %.2fdB  ", atten);
  tft.setTextSize(2);
  tft.setFreeFont(FSS18);
  tft.drawString(buffer1, 150, 120, 1);
  tft.setTextSize(1);
  tft.setFreeFont(FSS24);
}

void sourceUpdate()
{
  if (turnedRightFlag)
  {
    oldsource = source;
    milOnButton = millis();
    if (oldsource < 4)
    {
      source++;
    }
    else
    {
      source = 1;
    }
    setIO();
    // Set flag back to false so we can watch for the next move
    turnedRightFlag = false;
  }
  else if (turnedLeftFlag)
  {
    oldsource = source;
    milOnButton = millis();
    if (source > 1)
    {
      source--;
    }
    else
    {
      source = 4;
    }
    if (!backlight)
    {
      backlight = ACTIVE;
      digitalWrite(TFT_BL, HIGH); // Turn on backlight
    }
    setIO();
    // Set flag back to false so we can watch for the next move
    turnedLeftFlag = false;
  }
}

void RC5Update()
{
  /*
  System addresses and codes used here match RC-5 infra-red codes for amplifiers (and CDs)
  */
  u_char toggle;
  u_char address;
  u_char command;
  // Poll for new RC5 command
  if (rc5.read(&toggle, &address, &command))
  {
    /* For Debug
    Serial.print("a:");
    Serial.print(address);
    Serial.print(" c:");
    Serial.print(command);
    Serial.print(" t:");
    Serial.println(toggle);*/

    if (address == 0x10) // standard system address for preamplifier
    {
      switch (command)
      {
      case 1:
        // Phono
        if ((oldtoggle != toggle))
        {
          if (!backlight)
          {
            unMute(); // unmute output
          }
          oldsource = source;
          source = 1;
          setIO();
        }
        break;
      case 3:
        // Tuner
        if ((oldtoggle != toggle))
        {
          if (!backlight)
          {
            unMute(); // unmute output
          }
          oldsource = source;
          source = 4;
          setIO();
        }
        break;
      case 7:
        // CD
        if ((oldtoggle != toggle))
        {
          if (!backlight)
          {
            unMute(); // unmute output
          }
          oldsource = source;
          source = 3;
          setIO();
        }
        break;
      case 8:
        // Media
        if ((oldtoggle != toggle))
        {
          if (!backlight)
          {
            unMute(); // unmute output
          }
          oldsource = source;
          source = 2;
          setIO();
        }
        break;
      case 13:
        // Mute
        if ((oldtoggle != toggle))
        {
          toggleMute();
        }
        break;
      case 16:
        // Increase Vol / reduce attenuation
        if (isMuted)
        {
          unMute();
        }
        if (volume < 0)
        {
          volume = volume + 1;
          setVolume();
        }
        break;
      case 17:
        // Reduce Vol / increase attenuation
        if (isMuted)
        {
          unMute();
        }
        if (volume > -447)
        {
          volume = volume - 1;
          setVolume();
        }
        break;
      case 59:
        // Display Toggle
        if ((oldtoggle != toggle))
        {
          if (backlight)
          {
            backlight = STANDBY;
            digitalWrite(TFT_BL, LOW); // Turn off backlight
            // mute();                    // mute output
          }
          else
          {
            backlight = ACTIVE;
            digitalWrite(TFT_BL, HIGH); // Turn on backlight
            // unMute(); // unmute output
          }
        }
        break;
      default:
        break;
      }
    }
    else if (address == 0x14) // system address for CD
    {
      if ((oldtoggle != toggle))
      {
        if (command == 53) // Play
        {
          if (!backlight)
          {
            unMute(); // unmute output
          }
          oldsource = source;
          source = 3;
          setIO();
        }
      }
    }
    oldtoggle = toggle;
  }
}

void unMute()
{
  if (!backlight)
  {
    backlight = ACTIVE;
    digitalWrite(TFT_BL, HIGH);
  }
  isMuted = 0;
  //  set volume
  setVolume();
  // set source
  setIO();
}

void mute()
{
  isMuted = 1;
  Muses.mute();
  tft.setTextSize(2);
  tft.setFreeFont(FSS18);
  tft.drawString("    Muted    ", 160, 120, 1);
  tft.setTextSize(1);
  tft.setFreeFont(FSS24);
}

void toggleMute()
{
  if (isMuted)
  {
    unMute();
  }
  else
  {
    mute();
  }
}

void RotaryUpdate()
{
  switch (state)
  {
  case STATE_RUN:
    volumeUpdate();
    break;
  case STATE_IO:
    sourceUpdate();
    if ((millis() - milOnButton) > TIME_EXITSELECT * 1000)
    {
      state = STATE_RUN;
    }
    break;
  default:
    break;
  }
}

void setIO()
{
  MCP.write1((oldsource - 1), LOW); // Reset source select to NONE
  MCP.write1((source - 1), HIGH);   // Set new source
  preferences.putUInt("SOURCE", source);
  if (isMuted)
  {
    if (!backlight)
    {
      backlight = ACTIVE;
      digitalWrite(TFT_BL, HIGH);
    }
    isMuted = 0;
    tft.fillScreen(TFT_WHITE);
    // set volume
    setVolume();
  }
  tft.drawString(inputName[source - 1], 150, 200, 1);
}

// This section of code runs only once at start-up.
void setup()
{
  Serial.begin(115200);

  // This tells the library that the encoder has no pull-up resistors and to use ESP32 internal ones
  rotaryEncoder.setEncoderType(EncoderType::FLOATING);

  // The encoder will only return -1, 0, or 1, and will not wrap around.
  rotaryEncoder.setBoundaries(-1, 1, false);

  // The function specified here will be called every time the knob is turned
  // and the current value will be passed to it
  rotaryEncoder.onTurned(&knobCallback);

  // The function specified here will be called every time the button is pushed and
  // the duration (in milliseconds) that the button was down will be passed to it
  rotaryEncoder.onPressed(&buttonCallback);

  // This is where the rotary inputs are configured and the interrupts get attached
  rotaryEncoder.begin();

  // Initialise the TFT screen
  tft.init();
  tft.setRotation(1);

  // Set text datum to middle centre
  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(FSS18);

  // Clear the screen
  tft.fillScreen(TFT_WHITE);

  // show software version briefly in display
  tft.setTextColor(TFT_BLUE, TFT_WHITE);
  tft.drawString(softTitle1, 160, 80, 1);
  tft.drawString(softTitle2, 160, 120, 1);
  tft.drawString("SW ver " VERSION_NUM, 160, 160, 1);
  delay(2000);
  tft.fillScreen(TFT_WHITE);

  // Connect to Wi-Fi
  // Serial.print("Connecting to ");
  // Serial.println(ssid);
  tft.drawString("Connecting to network",160, 80, 1);
  WiFi.begin(ssid, password);
  tft.setCursor(10, 100);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
    tft.print(".");
  }
  tft.println("");
  tft.drawString("WiFi connected.",160, 160, 1);
  delay(500);
  tft.setFreeFont(FSS24);
  tft.fillScreen(TFT_WHITE);
  
  // Initialize clock
  initTime("WET0WEST,M3.5.0/1,M10.5.0");
  //configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  displayLocalTime();

  // This initialises the Source select pins as outputs, all deselected (i.e. o/p=low)
  MCP.begin();
  MCP.pinMode8(0x00); //  0 = output , 1 = input

  // Initialize muses (SPI, pin modes)...
  Muses.begin();
  Muses.setExternalClock(false); // must be set!
  Muses.setZeroCrossingOn(true);
  Muses.mute();
  // Load saved settings (volume, balance, source)
  preferences.begin("settings", RW_MODE);
  source = preferences.getUInt("SOURCE", 1);
  volume = preferences.getInt("VOLUME", -447);
  if (volume > 0)
    volume = -447;
  delay(10);
  // set startup volume
  setVolume();
  // set source
  setIO();
  // unmute
  isMuted = 0;
}

void loop()
{
  displayLocalTime();
  RC5Update();
  RotaryUpdate();
}
