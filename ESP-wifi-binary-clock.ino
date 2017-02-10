/*
 Jan 2017 - Leon van den Beukel

 Description: ESP12 / ESP8266 binary clock 

 - It uses 3 shift registers (74HC595) to output the LED's
 
*/

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Ticker.h>

char ssid[] = "***";      // your network SSID (name)
char pass[] = "***";      // your network password

unsigned int localPort = 2390;  // local port to listen for UDP packets

IPAddress timeServerIP;         // time.nist.gov NTP server address
const char* ntpServerName = "time.nist.gov";
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
WiFiUDP udp;

// Pin connected to ST_CP of 74HC595
int latchPin = 16;
// Pin connected to SH_CP of 74HC595
int clockPin = 14;
// Pin connected to DS of 74HC595
int dataPin = 12;

unsigned long epoch;

Ticker ticker;
Ticker tickerRefresh;
int timeZoneCorrection = 1;
const int rebootEvery = 60 * 60 * 24;

/* 
 *  The first 4 bits are the ten part, the last 4 bits are the one part
 *  So the tenPart bits are left bit shifted 
 */
byte getTimeByte(int timePart) {
  int tenPart = timePart / 10;
  int onePart = timePart % 10;
  return onePart |= tenPart << 4;
}

/* 
 *  Gets executed every 1 second
 */
void tick() {
  int hours   = (epoch % 86400L) / 3600;
  int minutes = (epoch % 3600) / 60;
  int seconds =  epoch % 60;
  hours += timeZoneCorrection;

  byte dataHours   = getTimeByte(hours);
  byte dataMinutes = getTimeByte(minutes);
  byte dataSeconds = getTimeByte(seconds);

  digitalWrite(latchPin, LOW);
  shiftOut(dataPin, clockPin, MSBFIRST, dataSeconds);  
  shiftOut(dataPin, clockPin, MSBFIRST, dataMinutes);  
  shiftOut(dataPin, clockPin, MSBFIRST, dataHours);  
  digitalWrite(latchPin, HIGH);

  // Add 1 second
  epoch++;  
  Serial.println(epoch);
}


void disableLeds() {
  digitalWrite(latchPin, LOW);
  shiftOut(dataPin, clockPin, MSBFIRST, 0);  
  shiftOut(dataPin, clockPin, MSBFIRST, 0);  
  shiftOut(dataPin, clockPin, MSBFIRST, 0);  
  digitalWrite(latchPin, HIGH);  
}

/*
 * Setup routine
 */
void setup()
{
  disableLeds();
  delay(300);
  Serial.begin(115200);
  Serial.println();
  Serial.println();

  // Connect to WiFi network
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.println("Starting UDP");
  udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(udp.localPort());

  // Shift register pins
  pinMode(latchPin, OUTPUT);
  pinMode(clockPin, OUTPUT);
  pinMode(dataPin, OUTPUT);   

  getDateTime();
  tickerRefresh.attach(rebootEvery, reboot);
}

void loop()
{
  // Do nothing in the main loop (only using ticker callbacks)
}

void reboot() {
  disableLeds();
  ESP.restart();
}

/* 
 *  The following code is based on the NTP Client example for ESP8266, more info on:
 *  https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WiFi/examples/NTPClient
 */
void getDateTime() {

  disableLeds();
  bool timeSuccess = false;
  ticker.detach();

  while (!timeSuccess) {
  
    //get a random server from the pool
    WiFi.hostByName(ntpServerName, timeServerIP); 
  
    sendNTPpacket(timeServerIP); // send an NTP packet to a time server
    // wait to see if a reply is available
    delay(1000);
    
    int cb = udp.parsePacket();
    if (!cb) {
      Serial.println("no packet yet");
    }
    else {      
      Serial.print("packet received, length=");
      Serial.println(cb);
      // We've received a packet, read the data from it
      udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
  
      //the timestamp starts at byte 40 of the received packet and is four bytes,
      // or two words, long. First, esxtract the two words:
  
      unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
      unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
      // combine the four bytes (two words) into a long integer
      // this is NTP time (seconds since Jan 1 1900):
      unsigned long secsSince1900 = highWord << 16 | lowWord;
      Serial.print("Seconds since Jan 1 1900 = " );
      Serial.println(secsSince1900);
  
      // now convert NTP time into everyday time:
      Serial.print("Unix time = ");
      // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
      const unsigned long seventyYears = 2208988800UL;
      // subtract seventy years:
      epoch = secsSince1900 - seventyYears;
      // print Unix time:
      Serial.println(epoch);  
  
      // print the hour, minute and second:
      Serial.print("The UTC time is ");       // UTC is the time at Greenwich Meridian (GMT)
      Serial.print((epoch  % 86400L) / 3600); // print the hour (86400 equals secs per day)
      Serial.print(':');
      if ( ((epoch % 3600) / 60) < 10 ) {
        // In the first 10 minutes of each hour, we'll want a leading '0'
        Serial.print('0');
      }
      Serial.print((epoch  % 3600) / 60); // print the minute (3600 equals secs per minute)
      Serial.print(':');
      if ( (epoch % 60) < 10 ) {
        // In the first 10 seconds of each minute, we'll want a leading '0'
        Serial.print('0');
      }
      Serial.println(epoch % 60); // print the second 

      timeSuccess = true;      

      // Enable ticker every second
      ticker.attach(1, tick);
    }
    // wait x seconds before asking for the time again
    delay(1000);
  }  
}

// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress& address)
{
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}
