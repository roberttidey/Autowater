/*
 R. J. Tidey 2018/06/27
 Automatic plant watering
 Can measure moisture and water to meet a target moisture 
 or can do a regular watering burst
 Web software update service included
 WifiManager can be used to config wifi network
 
 Temperature reporting Code based on work by Igor Jarc
 Some code based on https://github.com/DennisSc/easyIoT-ESPduino/blob/master/sketches/ds18b20.ino
 
 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 version 2 as published by the Free Software Foundation.
 */
#define ESP8266

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <DNSServer.h>
#include <WiFiManager.h>
#include "FS.h"
#include <TimeLib.h>
#include <NtpClientLib.h>
#include "wkEvents.h"

//put -1 s at end
int unusedPins[11] = {0,2,4,15,16,-1,-1,-1,-1,-1,-1};

/*
Wifi Manager Web set up
If WM_NAME defined then use WebManager
*/
#define WM_NAME "autowater"
#define WM_PASSWORD "password"
#ifdef WM_NAME
	WiFiManager wifiManager;
#endif
char wmName[33];

//uncomment to use a static IP
//#define WM_STATIC_IP 192,168,0,100
//#define WM_STATIC_GATEWAY 192,168,0,1

int timeInterval = 50;
#define WIFI_CHECK_TIMEOUT 30000
#define BUTTON_INTTIME_MIN 250
unsigned long elapsedTime;
unsigned long wifiCheckTime;
unsigned long lastWaterTime;
unsigned long storedWaterTime = 0;;
unsigned long startUpTime;
unsigned long buttonIntTime;
unsigned long timeTillNext;
//holds the current upload
File fsUploadFile;

int8_t timeZone = 0;
int8_t minutesTimeZone = 0;
time_t currentTime;

//For update service
String host = "esp8266-water";
const char* update_path = "/firmware";
const char* update_username = "admin";
const char* update_password = "password";

//AP definitions
#define AP_SSID "ssid"
#define AP_PASSWORD "password"
#define AP_MAX_WAIT 10
String macAddr;

#define AP_AUTHID "12345678"
#define AP_PORT 80

ESP8266WebServer server(AP_PORT);
ESP8266HTTPUpdateServer httpUpdater;
HTTPClient cClient;

wkEvents wkSchedule;

//Comment out for no authorisation else uses same authorisation as EIOT server
#define EIOT_RETRIES 10


//general variables
#define CONFIG_FILE "/waterConfig.txt"
#define LOG_FILE "/waterLog.txt"
#define NEXT_WATER_FILE "/storedWaterTime.txt"
#define WATER_PIN 5
#define MOISTURE_PIN 14
#define PUSH_BUTTON1 13
#define PUSH_BUTTON2 12
#define MODE_INTERVAL 1
#define MODE_MOISTURE 2
#define MODE_SCHEDULE 3
#define REQUEST_INTERVAL 1
#define REQUEST_MOISTURE 2
#define REQUEST_SCHEDULE 3
#define REQUEST_BUTTON 4
#define REQUEST_WEB 5

int waterMode = 0;
int waterInterval = 900; // seconds
int waterPulse = 2000; // milliseconds
int moistureLevel = 512;
int sleepInterval = 900; //seconds
String schedule = "null";
int pulse;
int requestPulse = 0;
int ntpRetries = 0;
#define NTP_RETRIES 5
#define NTP_SHORTINTERVAL 10
#define SLEEP_MODE_OFF 0
#define SLEEP_MODE_DEEP 1
#define SLEEP_MODE_LIGHT 2
int sleepMode = SLEEP_MODE_DEEP;

void ICACHE_RAM_ATTR  delaymSec(unsigned long mSec) {
	unsigned long ms = mSec;
	while(ms > 100) {
		delay(100);
		ms -= 100;
		ESP.wdtFeed();
	}
	delay(ms);
	ESP.wdtFeed();
	yield();
}

void ICACHE_RAM_ATTR  delayuSec(unsigned long uSec) {
	unsigned long us = uSec;
	while(us > 100000) {
		delay(100);
		us -= 100000;
		ESP.wdtFeed();
	}
	delayMicroseconds(us);
	ESP.wdtFeed();
	yield();
}

void ICACHE_RAM_ATTR buttonPushInterrupt() {
	unsigned long m = millis();
	//Ignore fast edges
	if(((m-buttonIntTime) > BUTTON_INTTIME_MIN) && !requestPulse) {
		requestPulse = REQUEST_BUTTON;
	}
	buttonIntTime = m;
}


void unusedIO() {
	int i;
	
	for(i=0;i<11;i++) {
		if(unusedPins[i] < 0) {
			break;
		} else if(unusedPins[i] != 16) {
			pinMode(unusedPins[i],INPUT_PULLUP);
		} else {
			pinMode(16,INPUT_PULLDOWN_16);
		}
	}
}

/*
  Log event
*/
void logEvent(int eventType, String event) {
	currentTime = now(); 
	File f = SPIFFS.open(LOG_FILE, "a");
	f.print(NTP.getDateStr(currentTime) + " " + NTP.getTimeStr(currentTime) + "," + String(eventType) + "," + event + "\n");
	f.close();
}


/*
  Connect to local wifi with retries
  If check is set then test the connection and re-establish if timed out
*/
int wifiConnect(int check) {
	if(check) {
		if((elapsedTime - wifiCheckTime) * timeInterval > WIFI_CHECK_TIMEOUT) {
			if(WiFi.status() != WL_CONNECTED) {
				Serial.println(F("Wifi connection timed out. Try to relink"));
			} else {
				wifiCheckTime = elapsedTime;
				return 1;
			}
		} else {
			return 0;
		}
	}
	wifiCheckTime = elapsedTime;
#ifdef WM_NAME
	Serial.println(F("Set up managed Web"));
#ifdef WM_STATIC_IP
	wifiManager.setSTAStaticIPConfig(IPAddress(WM_STATIC_IP), IPAddress(WM_STATIC_GATEWAY), IPAddress(255,255,255,0));
#endif
	wifiManager.setConfigPortalTimeout(180);
	//Revert to STA if wifimanager times out as otherwise APA is left on.
	strcpy(wmName, WM_NAME);
	strcat(wmName, macAddr.c_str());
	wifiManager.autoConnect(wmName, WM_PASSWORD);
	WiFi.mode(WIFI_STA);
#else
	Serial.println(F("Set up manual Web"));
	int retries = 0;
	Serial.print(F("Connecting to AP"));
	#ifdef AP_IP
		IPAddress addr1(AP_IP);
		IPAddress addr2(AP_DNS);
		IPAddress addr3(AP_GATEWAY);
		IPAddress addr4(AP_SUBNET);
		WiFi.config(addr1, addr2, addr3, addr4);
	#endif
	WiFi.begin(AP_SSID, AP_PASSWORD);
	while (WiFi.status() != WL_CONNECTED && retries < AP_MAX_WAIT) {
		delaymSec(1000);
		Serial.print(".");
		retries++;
	}
	Serial.println("");
	if(retries < AP_MAX_WAIT) {
		Serial.print("WiFi connected ip ");
		Serial.print(WiFi.localIP());
		Serial.printf(":%d mac %s\r\n", AP_PORT, WiFi.macAddress().c_str());
		return 1;
	} else {
		Serial.println(F("WiFi connection attempt failed")); 
		return 0;
	} 
#endif
	//wifi_set_sleep_type(LIGHT_SLEEP_T);
}


void initFS() {
	if(!SPIFFS.begin()) {
		Serial.println(F("No SIFFS found. Format it"));
		if(SPIFFS.format()) {
			SPIFFS.begin();
		} else {
			Serial.println(F("No SIFFS found. Format it"));
		}
	} else {
		Serial.println(F("SPIFFS file list"));
		Dir dir = SPIFFS.openDir("/");
		while (dir.next()) {
			Serial.print(dir.fileName());
			Serial.print(F(" - "));
			Serial.println(dir.fileSize());
		}
	}
}

String getContentType(String filename){
  if(server.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

bool handleFileRead(String path){
  Serial.printf_P(PSTR("handleFileRead: %s\r\n"), path.c_str());
  if(path.endsWith("/")) path += "index.htm";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){
    if(SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

void handleFileUpload(){
  if(server.uri() != "/edit") return;
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    String filename = upload.filename;
    if(!filename.startsWith("/")) filename = "/"+filename;
    Serial.printf_P(PSTR("handleFileUpload Name: %s\r\n"), filename.c_str());
    fsUploadFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if(upload.status == UPLOAD_FILE_WRITE){
    Serial.printf_P(PSTR("handleFileUpload Data: %d\r\n"), upload.currentSize);
    if(fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if(upload.status == UPLOAD_FILE_END){
    if(fsUploadFile)
      fsUploadFile.close();
    Serial.printf_P(PSTR("handleFileUpload Size: %d\r\n"), upload.totalSize);
  }
}

void handleFileDelete(){
  if(server.args() == 0) return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  Serial.printf_P(PSTR("handleFileDelete: %s\r\n"),path.c_str());
  if(path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if(!SPIFFS.exists(path))
    return server.send(404, "text/plain", "FileNotFound");
  SPIFFS.remove(path);
  server.send(200, "text/plain", "");
  path = String();
}

void handleFileCreate(){
  if(server.args() == 0)
    return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  Serial.printf_P(PSTR("handleFileCreate: %s\r\n"),path.c_str());
  if(path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if(SPIFFS.exists(path))
    return server.send(500, "text/plain", "FILE EXISTS");
  File file = SPIFFS.open(path, "w");
  if(file)
    file.close();
  else
    return server.send(500, "text/plain", "CREATE FAILED");
  server.send(200, "text/plain", "");
  path = String();
}

void handleFileList() {
  if(!server.hasArg("dir")) {server.send(500, "text/plain", "BAD ARGS"); return;}
  
  String path = server.arg("dir");
  Serial.printf_P(PSTR("handleFileList: %s\r\n"),path.c_str());
  Dir dir = SPIFFS.openDir(path);
  path = String();

  String output = "[";
  while(dir.next()){
    File entry = dir.openFile("r");
    if (output != "[") output += ',';
    bool isDir = false;
    output += "{\"type\":\"";
    output += (isDir)?"dir":"file";
    output += "\",\"name\":\"";
    output += String(entry.name()).substring(1);
    output += "\"}";
    entry.close();
  }
  output += "]";
  server.send(200, "text/json", output);
}

void handleMinimalUpload() {
  char temp[700];

  snprintf ( temp, 700,
    "<!DOCTYPE html>\
    <html>\
      <head>\
        <title>ESP8266 Upload</title>\
        <meta charset=\"utf-8\">\
        <meta http-equiv=\"X-UA-Compatible\" content=\"IE=edge\">\
        <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\
      </head>\
      <body>\
        <form action=\"/edit\" method=\"post\" enctype=\"multipart/form-data\">\
          <input type=\"file\" name=\"data\">\
          <input type=\"text\" name=\"path\" value=\"/\">\
          <button>Upload</button>\
         </form>\
      </body>\
    </html>"
  );
  server.send ( 200, "text/html", temp );
}

void handleSpiffsFormat() {
	SPIFFS.format();
	server.send(200, "text/json", "format complete");
}

/*
  load schedule
*/
void loadSchedule() {
	File f = SPIFFS.open("/" + schedule, "r");
	if(f) {
		wkSchedule.clearEvents();
		wkSchedule.loadEvents(f);
		f.close();
	}
}

/*
  save schedule
*/
void saveSchedule() {
	File f = SPIFFS.open("/" + schedule, "w");
	f.print("#Weekly Schedule\n");
	f.print("#Each line is eventCode(0-255),day(1-7),hour(0-23),minute(0-59),second(0-59)\n");
	wkSchedule.saveEvents(f);
	f.close();
}

/*
  Get config
*/
void getConfig() {
	String line = "";
	int config = 0;
	File f = SPIFFS.open(CONFIG_FILE, "r");
	if(f) {
		while(f.available()) {
			line =f.readStringUntil('\n');
			line.replace("\r","");
			if(line.length() > 0 && line.charAt(0) != '#') {
				switch(config) {
					case 0: host = line;break;
					case 1: waterMode = line.toInt();break;
					case 2: waterInterval = line.toInt();break;
					case 3: waterPulse = line.toInt();break;
					case 4: moistureLevel = line.toInt();break;
					case 5: schedule = line;break;
					case 6: timeZone = line.toInt();break;
					case 7: minutesTimeZone = line.toInt();break;
					case 8:
						sleepInterval =line.toInt();
						Serial.println(F("Config loaded from file OK"));
				}
				config++;
			}
		}
		f.close();
		if(sleepInterval < 15) sleepInterval = 15;
		if(sleepInterval > 3600) sleepInterval = 3660;
		if(waterInterval < sleepInterval) waterInterval = sleepInterval;
		Serial.println("Config loaded");
		Serial.print(F("host:"));Serial.println(host);
		Serial.print(F("waterMode:"));Serial.println(waterMode);
		Serial.print(F("waterInterval:"));Serial.println(waterInterval);
		Serial.print(F("waterPulse:"));Serial.println(waterPulse);
		Serial.print(F("moistureLevel:"));Serial.println(moistureLevel);
		Serial.print(F("schedule:"));Serial.println(schedule);
		Serial.print(F("timeZone:"));Serial.println(timeZone);
		Serial.print(F("minutesTimeZone:"));Serial.println(minutesTimeZone);
		Serial.print(F("sleepInterval:"));Serial.println(sleepInterval);
		
	} else {
		Serial.println(String(CONFIG_FILE) + " not found");
	}
}

/*
  Save config
*/
void saveConfig() {
	File f = SPIFFS.open(CONFIG_FILE, "w");
	if(f) {
		f.print(String(F("#Config file for Auto Plant watering")) + "\n");
		f.print(String(F("# lines are host;waterMode;waterInterval,waterPulse,moistureLevel;schedule;timeZone,timeZoneMinutes;sleepInterval")) + "\n");
		f.print(host + "\n");
		f.print(String(waterMode) + "\n");
		f.print(String(waterInterval) + "\n");
		f.print(String(waterPulse) + "\n");
		f.print(String(moistureLevel) + "\n");
		f.print(schedule + "\n");
		f.print(String(timeZone) + "\n");
		f.print(String(minutesTimeZone) + "\n");
		f.print(String(sleepInterval) + "\n");
		f.close();
	}
}


/*
  reload Config
*/
void reloadConfig() {
	if (server.arg("auth") != AP_AUTHID) {
		Serial.println("Unauthorized");
		server.send(401, "text/html", "Unauthorized");
	} else {
		getConfig();
		loadSchedule();
		server.send(200, "text/html", "Config reload");
	}
}

/*
  requestPulse
*/
void webPulse() {
	if (server.arg("auth") != AP_AUTHID) {
		Serial.println(F("Unauthorized"));
		server.send(401, "text/html", "Unauthorized");
	} else {
		pulse = server.arg("p").toInt();
		if(pulse == 0) pulse = waterPulse;
		requestPulse = REQUEST_WEB;
		server.send(200, "text/html", "Pulse requested");
	}
}

/*
  Check moisture level
*/
int moisture() {
	return analogRead(A0);
}

/*
  Send a water pulse
*/
void pulseWater() {
	unsigned long
	currentTime = now(); 
	Serial.println("PulseWater Reason " + String(requestPulse));
	digitalWrite(WATER_PIN, 1);
	delaymSec(pulse);
	digitalWrite(WATER_PIN, 0);
	logEvent(requestPulse, String(pulse));
}

/*
  Update Config from Web
*/
void updateConfig() {
	String e;
	String f;
	int i,j,k;
	int scheduleChanged = 0;
	if (server.arg("auth") != AP_AUTHID) {
		Serial.println(F("Unauthorized"));
		server.send(401, "text/html", "Unauthorized");
	} else {
		i = 0;
		j = 0;
		e = server.arg("p");
		for(k=0;j>=0;k++) {
			j = e.indexOf(',', i);
			if(j<0) {
				f = e.substring(i);
			} else {
				f = e.substring(i,j);
			}
			switch(k) {
				case 0: host = f;break;
				case 1: waterMode = f.toInt();break;
				case 2: waterInterval = f.toInt();break;
				case 3: waterPulse = f.toInt();break;
				case 4: moistureLevel = f.toInt();break;
				case 5: if(f != schedule) scheduleChanged = 1;
						schedule = f;
						break;
				case 6: timeZone = f.toInt();break;
				case 7: minutesTimeZone = f.toInt();break;
				case 8: sleepInterval = f.toInt();break;
			}
			i = j + 1;
		}
		if(sleepInterval < 15) sleepInterval = 15;
		if(sleepInterval > 3600) sleepInterval = 3660;
		if(waterInterval < sleepInterval) waterInterval = sleepInterval;
		saveConfig();
		if(scheduleChanged) loadSchedule();
		server.send(200, "text/html", "Config Saved");
	}
}

/*
  Update Event from Web (Add or delete)
  c = 5 for add 4 for delete
*/
void updateEvent(int c) {
	String e;
	int f[5] = {0};
	int i,j,k;
	if (server.arg("auth") != AP_AUTHID) {
		Serial.println(F("Unauthorized"));
		server.send(401, "text/html", "Unauthorized");
	} else {
		e = server.arg("p");
		Serial.println("Update " + String(c) + " par:" + e);
		i = 0;
		for(k=0;k<c;k++) {
			j = e.indexOf(',', i);
			if(j<0) {
				f[k] = e.substring(i).toInt();
				break;
			}
			f[k] = e.substring(i,j).toInt();
			i = j + 1;
		}
		if(c > 4) {
			wkSchedule.addEvent(f[0],f[1],f[2],f[3],f[4]);
			server.send(200, "text/html", "Event added");
		} else {
			Serial.println("Delete " + String(f[0]) + " " + String(f[1]) + " " + String(f[2]) + " " + String(f[3]));
			wkSchedule.deleteEvent(f[0],f[1],f[2],f[3]);
			server.send(200, "text/html", "Event added");
		}
		saveSchedule();
		server.send(401, "text/html", "bad event");
	}
}

/*
  Add Event from Web
*/
void addEvent() {
	updateEvent(5);
}

/*
  Delete Event from Web
*/
void deleteEvent() {
	updateEvent(4);
}

/*
  getstoredWaterTime
*/
void getstoredWaterTime() {
	File f = SPIFFS.open(NEXT_WATER_FILE, "r");
	if(f) {
		storedWaterTime = strtoul(f.readStringUntil('\n').c_str(), NULL, 10);
		f.close();
	} else {
		storedWaterTime = now() + waterInterval;
		savestoredWaterTime();
	}
}

/*
  savestoredWaterTime
*/
void savestoredWaterTime() {
	File f = SPIFFS.open(NEXT_WATER_FILE, "w");
	f.print(String(storedWaterTime) + "\n");
	f.close();
}

/*
  Check watering
*/
unsigned long checkWater() {
	int saveFlag = 0;
	Serial.println("current:stored " + String(currentTime) + ":" + String(storedWaterTime));
	getstoredWaterTime();
	switch(waterMode) {
		case MODE_INTERVAL: 
			//interval mode
			if(currentTime > storedWaterTime) {
				requestPulse = REQUEST_INTERVAL;
				pulse = waterPulse;
				storedWaterTime = storedWaterTime + waterInterval- pulse/1000;
				saveFlag = 1;
			}
			//sanity check if time has got out of whack
			if(storedWaterTime > (currentTime + 2 * waterInterval)) {
				Serial.println(F("Time discrepancy. Adjusting next water time"));
				storedWaterTime = currentTime + waterInterval;
				saveFlag = 1;
			}
			break;
		case MODE_MOISTURE:
			//auto moisture mode
			if(moisture() > moistureLevel) {
				requestPulse = REQUEST_MOISTURE;
				pulse = waterPulse;
			}
			storedWaterTime = currentTime + waterInterval - pulse/1000;
			break;
		case MODE_SCHEDULE:
			//schedule mode
			if(currentTime > storedWaterTime) {
				int currentIndex = wkSchedule.getEventIndex(weekday(currentTime),hour(currentTime),minute(currentTime),second(currentTime));
				Serial.println("currentIndex " + String(currentIndex));
				if(wkSchedule.getEventCode(currentIndex)) {
					Serial.println(F("Scheduled Pulse"));
					requestPulse = REQUEST_SCHEDULE;
					pulse = waterPulse;
				}
				//adjust storedWaterTime
				storedWaterTime = currentTime + wkSchedule.getSecondsToNext(weekday(currentTime),hour(currentTime),minute(currentTime),second(currentTime)) - pulse/1000;
				saveFlag = 1;
			}
			//sanity check if time has got out of whack
			if(storedWaterTime > (currentTime + SECONDSPERWEEK)) {
				Serial.println(F("Time discrepancy. Adjusting next water time"));
				storedWaterTime = currentTime + wkSchedule.getSecondsToNext(weekday(currentTime),hour(currentTime),minute(currentTime),second(currentTime)) - pulse/1000;
				saveFlag = 1;
			}
			break;
	}
	if(saveFlag) savestoredWaterTime();
	//return time till next event due
	return storedWaterTime - currentTime;
}

/*
 Send report to easyIOTReport
 if digital = 1, send digital else analog
*/
void easyIOTReport(String node, float value, int digital) {
	int retries = EIOT_RETRIES;
	int responseOK = 0;
	int httpCode;
	String url = String(EIOT_IP_ADDRESS) + node;
	// generate EasIoT server node URL
	if(digital == 1) {
		if(value > 0)
			url += "/ControlOn";
		else
			url += "/ControlOff";
	} else {
		url += "/ControlLevel/" + String(value);
	}
	Serial.print("POST data to URL: ");
	Serial.println(url);
	while(retries > 0) {
		cClient.setAuthorization(EIOT_USERNAME, EIOT_PASSWORD);
		cClient.begin(url);
		httpCode = cClient.GET();
		if (httpCode > 0) {
			if (httpCode == HTTP_CODE_OK) {
				String payload = cClient.getString();
				Serial.println(payload);
				responseOK = 1;
			}
		} else {
			Serial.printf("[HTTP] POST... failed, error: %s\n", cClient.errorToString(httpCode).c_str());
		}
		cClient.end();
		if(responseOK)
			break;
		else
			Serial.println(F("Retrying EIOT report"));
		retries--;
	}
	Serial.println();
	Serial.println(F("Connection closed"));
}

/*
  getStatus
*/
void getStatus() {
	String response;
	currentTime = now();
	int cDay = weekday(currentTime);
	int cHour = hour(currentTime);
	int cMinute = minute(currentTime);
	int cSecond = second(currentTime);
	int i;
	
	File f = SPIFFS.open("/" + schedule, "r");
	if(f) {
		response = "Time," + NTP.getDateStr(currentTime) + " " + NTP.getTimeStr(currentTime) + "<BR>";
		response += "Day Number," + String(cDay) + "<BR>";
		if (server.arg("load").toInt()) {
			wkSchedule.clearEvents();
			response += "Loaded events," + String(wkSchedule.loadEvents(f)) + "<BR>";
		} else {
			response += "Schedule Count," + String(wkSchedule.getEventCount()) + "<BR>";
		}
		
/*
		int c = wkSchedule.getEventCount();
		for(i=0;i<c;i++) {
			response += "Sch," + String(i) + " " + wkSchedule.getEventString(i) + "<BR>";
		}
*/		
		response += "GetCurrentEvent," + String(wkSchedule.getEventCode(cDay,cHour,cMinute,cSecond)) + "<BR>";
		response += "GetCurrentIndex," + String(wkSchedule.getEventIndex(cDay,cHour,cMinute,cSecond)) + "<BR>";
		response += "SecondsToNext," + String(wkSchedule.getSecondsToNext(cDay,cHour,cMinute,cSecond)) + "<BR>";
		response += "Moisture," + String(moisture()) + "<BR>";
		f.close();
	} else {
		response = "No schedule file";
	}
	server.send(200, "text/html", response);
}

/*
  Set up basic wifi, collect config from flash/server, initiate update server
*/
void setup() {
	startUpTime = millis();
	unusedIO();
	digitalWrite(WATER_PIN, 0);
	digitalWrite(MOISTURE_PIN, 1);
	pinMode(WATER_PIN, OUTPUT);
	pinMode(MOISTURE_PIN, OUTPUT);
	pinMode(PUSH_BUTTON1, INPUT_PULLUP);
	pinMode(PUSH_BUTTON2, INPUT_PULLUP);
	Serial.begin(115200);
	Serial.println(F("Set up filing system"));
	initFS();
	getConfig();
	loadSchedule();
	if(digitalRead(PUSH_BUTTON1) == 0) {
		sleepMode = SLEEP_MODE_OFF;
		Serial.println(F("Sleep override active"));
	}
	Serial.println(F("Set up Wifi services"));
	macAddr = WiFi.macAddress();
	macAddr.replace(":","");
	Serial.println(macAddr);
	wifiConnect(0);
	//Update service
	MDNS.begin(host.c_str());
	httpUpdater.setup(&server, update_path, update_username, update_password);
	Serial.println(F("Set up web server"));
	//Simple upload
	server.on("/upload", handleMinimalUpload);
	server.on("/reloadConfig", reloadConfig);
	server.on("/status", getStatus);
	server.on("/pulse", webPulse);
	server.on("/updateConfig", updateConfig);
	server.on("/addEvent", addEvent);
	server.on("/deleteEvent", deleteEvent);
	server.on("/format", handleSpiffsFormat);
	server.on("/list", HTTP_GET, handleFileList);
	//load editor
	server.on("/edit", HTTP_GET, [](){
    if(!handleFileRead("/edit.htm")) server.send(404, "text/plain", "FileNotFound");});
	//create file
	server.on("/edit", HTTP_PUT, handleFileCreate);
	//delete file
	server.on("/edit", HTTP_DELETE, handleFileDelete);
	//first callback is called after the request has ended with all parsed arguments
	//second callback handles file uploads at that location
	server.on("/edit", HTTP_POST, [](){ server.send(200, "text/plain", ""); }, handleFileUpload);
	//called when the url is not defined here
	//use it to load content from SPIFFS
	server.onNotFound([](){if(!handleFileRead(server.uri())) server.send(404, "text/plain", "FileNotFound");});
	server.begin();
	MDNS.addService("http", "tcp", 80);
	NTP.begin ("pool.ntp.org", timeZone, true, minutesTimeZone);
	NTP.setInterval(NTP_SHORTINTERVAL, 60);
	int i;
	attachInterrupt(PUSH_BUTTON2, buttonPushInterrupt, RISING);
	Serial.println(F("Set up complete"));
}

/*
  Main loop to read temperature and publish as required
*/
void loop() {
	int i;
	currentTime = now();
	//check if ntp time is set and not equal to epoch NTP
	if(((timeStatus() == timeSet) && currentTime != 2085978496) || ntpRetries > NTP_RETRIES) {
		// check if time is sensible (between 2017 and 2036)
		if(currentTime < 1500e6 || currentTime > 2085e6) {
			Serial.println(F("Bad Time sync. Skip processing"));
			logEvent(0, F("Bad Time sync. Skip processing"));
			timeTillNext = sleepInterval;
		} else {
			timeTillNext = checkWater();
			Serial.println("Time till next " + String(timeTillNext));
			logEvent(0, String(timeTillNext));
			if(requestPulse) {
				pulseWater();
				requestPulse = 0;
			}
		}
		if(sleepMode == SLEEP_MODE_DEEP) {
			Serial.println("Active Msec before sleep " + String(millis()-startUpTime));
			digitalWrite(MOISTURE_PIN, 0);
			if(timeTillNext > (sleepInterval)) {
				ESP.deepSleep(1e6*sleepInterval);
			} else {
				ESP.deepSleep(1e6*(timeTillNext + 30));
			}
		}
		for(i = 0;i < sleepInterval*1000/timeInterval; i++) {
			server.handleClient();
			wifiConnect(1);
			delaymSec(timeInterval);
			elapsedTime++;
			if(requestPulse) {
				pulseWater();
				requestPulse = 0;
			}
		}
	} else {
		Serial.println(F("Retry NTP sync"));
		logEvent(0, F("Retry NTP sync"));
		ntpRetries++;
		for(i = 0;i < (NTP_SHORTINTERVAL + 1)*1000/timeInterval; i++) {
			server.handleClient();
			wifiConnect(1);
			delaymSec(timeInterval);
			elapsedTime++;
		}
	}
}
