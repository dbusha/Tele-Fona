#include "Adafruit_FONA.h"
#include <SoftwareSerial.h>

// Pins
const static uint8_t DialPulsePin = 3;
const static uint8_t DialPin = 2;
const static uint8_t FonaReset = 4;
const static uint8_t LedPin = 5;
const static uint8_t RingingPin = 7;
const static uint8_t FonaTx = 8;
const static uint8_t FonaRx = 9;
const static uint8_t HookPin = 12;

// Hook state
enum HookState { HookDown = 0, HookUp = 1 };
HookState previousHookState = HookUp;
uint16_t lastHookChangedTime = 0;

// Call state
enum CallState {
    CallReady = 0,
    CallFailed = 1,
    CallUnknownState = 2,
    CallRinging = 3,
    CallInProgress = 4,
    CallSleeping = 5};

enum NetworkState {
  NotRegistered = 0,
  Registered = 1,
  Searching = 2,
  Denied = 3,
  UnknownState = 4,
  Roaming = 5
};

volatile uint8_t callState = CallReady;
volatile int8_t dialedNumber = -1;
volatile uint32_t lastDialPulseTime = 0;
volatile uint8_t pulseCount = 0;
volatile uint32_t dialToneStartTime = 0;
volatile uint32_t lastRingTime = 0;
volatile uint32_t lastCallStateUpdate = millis();

char phoneNumber[11] = {'0'};
uint8_t phoneNumberIndex = 0;

SoftwareSerial fonaSS = SoftwareSerial(FonaTx, FonaRx);
SoftwareSerial *fonaSerial = &fonaSS;

Adafruit_FONA fona = Adafruit_FONA(FonaReset);

void setup()
{
    // Wire up the pins
    pinMode(HookPin, INPUT);
    pinMode(DialPin, INPUT);
    pinMode(DialPulsePin, INPUT);
    pinMode(LedPin, OUTPUT);
    pinMode(FonaReset, OUTPUT);
    
    // Indicate that we're working...
    digitalWrite(LedPin, HIGH);
    
    Serial.begin(115200);
    Serial.println(F("FONA Initializing..."));

    // Comment this out when not hooked up over USB
    
     while(!Serial);

    // This should also reset the Fona
    fonaSerial->begin(4800);
    
    if (!fona.begin(*fonaSerial)) {
        Serial.println(F("Couldn't find FONA"));
    }
    Serial.println(F("FONA is OK"));

    // Configure Fona
    SetAudio();
    GetRssid();
    GetNetworkState();
    
    // Wire up interupts
    attachInterrupt(digitalPinToInterrupt(DialPin), DialHandler, CHANGE);
    attachInterrupt(digitalPinToInterrupt(DialPulsePin), DialPulsedHandler, RISING);
    attachInterrupt(digitalPinToInterrupt(RingingPin), RingingHandler, FALLING);

    // Done with setup
    digitalWrite(LedPin, LOW);
}


void loop()
{
    FlushSerial(); // is this necessary?
    
    HookState hookState = PollHookState();

    if (hookState == HookUp) {
        ProcessHookUp();
    }
    else if (hookState == HookDown) {
        ProcessHookDown();
    }
    previousHookState = hookState;
}


// -----------------------  Helper Methods -------------------------------
void SetAudio() 
{
    if (fona.setAudio(FONA_EXTAUDIO)) {
        fona.setVolume(50);
        Serial.println(F("External Audio set"));
    } else {
        Serial.println(F("Failed to set external audio"));
    }
}


void GetRssid()
{
    uint8_t rssid = 0;
    while (rssid == 0) {
        rssid = fona.getRSSI();
        digitalWrite(LedPin, HIGH);
        delay(500);
        digitalWrite(LedPin, LOW);
        delay(500);
    }
    Serial.print("RSSI: ");
    Serial.println(rssid);
}


void GetNetworkState()
{
    uint8_t networkState = 0;
    while (networkState != Registered){
        networkState = fona.getNetworkStatus();
        digitalWrite(LedPin, HIGH);
        delay(3000);
        digitalWrite(LedPin, LOW);
        delay(3000);
    }
    PrintNetworkStateDescription(networkState);
}


void FlushSerial() 
{
    // ThinkMe: Is this necessary?
    while(fona.available()) {
        fona.read();
    }
    while (Serial.available()) {
        Serial.read();
    }
}


HookState PollHookState()
{
    if ((millis() - lastHookChangedTime) < 50) {
        return;
    }
    HookState newHookState = (HookState)digitalRead(HookPin);
    if (newHookState == previousHookState) {
        return;
    }
    lastHookChangedTime = millis();
    Serial.print(F("Hook is "));
    Serial.println(newHookState == HookUp ? F("up") : F("down"));
    return newHookState;
}


void ProcessHookUp()
{
    if (callState == CallRinging && previousHookState == HookDown) 
    {
        Serial.println(F("answering call"));
        ResetDialTone();
        if (!fona.pickUp()) {
            Serial.println(F("Failed to pickup call")); 
        } else {
            Serial.println(F("Call answered"));
        }
        callState = CallInProgress;
        return;
    }

    if (callState == CallInProgress && dialedNumber > 0) 
    {
        Serial.println(F("ding"));
        fona.playDTMF('9');
        dialedNumber = -1;
        return;
    }

    if (callState == CallReady)
    {
        if (phoneNumberIndex == sizeof(phoneNumber)) 
        {
            Serial.print(F("Calling: "));
            Serial.println(String(phoneNumber));
            PrintCallStateDescription(fona.getCallStatus());
            fona.callPhone(phoneNumber);
            dialedNumber = -1;
            callState = CallInProgress;
        }
        else if (dialedNumber > 0) 
        {
            dialedNumber = -1;
            ResetDialTone();
            Serial.print(F("Number: "));
            Serial.println(String(phoneNumber));
        }
        else if (phoneNumberIndex == 0 && (dialToneStartTime == 0 || millis() - dialToneStartTime > 15300000)) {
            Serial.println(F("starting tone"));
            //fona.println(F("AT+STTONE=1,20,3000"));
            dialToneStartTime = millis();
        }
    }
}


void ProcessHookDown()
{
    ResetDialTone();
    if (previousHookState == HookUp) {
        ResetPhoneNumber();
        fona.hangUp();
        callState = CallReady;
        PrintCallStateDescription(fona.getCallStatus());
        PrintNetworkStateDescription(fona.getNetworkStatus());
    }
}


void ResetPhoneNumber()
{
    memset(phoneNumber, '0', sizeof(phoneNumber));
    phoneNumberIndex = 0;
}


void ResetDialTone()
{
    //fona.println(F("AT+STTONE=0"));
    dialToneStartTime = 0;
}


void PrintNetworkStateDescription(uint8_t networkStatus)
{
   Serial.print(F("Network State: "));
   switch(networkStatus) {
      case 0: Serial.println(F("Not registered")); break;
      case 1: Serial.println(F("Registered (home)")); break;
      case 2: Serial.println(F("Not registered (searching)"));break;
      case 3: Serial.println(F("Denied"));break;
      case 5: Serial.println(F("Registered roaming")); break;
      default: Serial.println(F("Unknown")); break;
   }
}


void PrintCallStateDescription(uint8_t call)
{
  Serial.print(F("Call State: "));
   switch(call) {
      case 0: Serial.println(F("Ready")); break;
      case 1: Serial.println(F("Failed")); break;
      case 3: Serial.println(F("Ringing"));break;
      case 4: Serial.println(F("InProgress"));break;
      case 5: Serial.println(F("Sleeping")); break;
      default: Serial.println(F("Unknown")); break;
   }
}


// ------------------------------- Interupt Handlers ------------------------------
void DialHandler()
{
    ResetDialTone();
    if (digitalRead(DialPin) == HIGH || pulseCount == 0)
        return;
    dialedNumber = pulseCount;
    Serial.print(F("Dialed "));
    Serial.println(dialedNumber);
    pulseCount = 0;
    phoneNumber[phoneNumberIndex] = (char)(dialedNumber+48);
    phoneNumberIndex++;
}


void DialPulsedHandler()
{
    if ((millis() - lastDialPulseTime) < 100)
        return;
    ++pulseCount;
    lastDialPulseTime = millis();
}


void RingingHandler()
{
    Serial.println(F("Ring..."));
    callState = CallRinging;
    lastRingTime = millis();
}
