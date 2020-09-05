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
#include "BaseConfig.h"
#include <TimeLib.h>
#include <NtpClientLib.h>
#include "wkEvents.h"

#define BUTTON_INTTIME_MIN 250
int timeInterval = 50;
unsigned long elapsedTime;
unsigned long lastWaterTime;
unsigned long storedWaterTime = 0;;
unsigned long startUpTime;
unsigned long buttonIntTime;
unsigned long timeTillNext;

int8_t timeZone = 0;
int8_t minutesTimeZone = 0;
time_t currentTime;

#define AP_AUTHID "14153143"

wkEvents wkSchedule;

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
uint64_t sleepuSec;

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

void ICACHE_RAM_ATTR buttonPushInterrupt() {
	unsigned long m = millis();
	//Ignore fast edges
	if(((m-buttonIntTime) > BUTTON_INTTIME_MIN) && !requestPulse) {
		requestPulse = REQUEST_BUTTON;
	}
	buttonIntTime = m;
}

/*
  Log event
*/
void logEvent(int eventType, String event) {
	currentTime = now(); 
	File f = FILESYS.open(LOG_FILE, "a");
	f.print(NTP.getDateStr(currentTime) + " " + NTP.getTimeStr(currentTime) + "," + String(eventType) + "," + event + "\n");
	f.close();
}

/*
  load schedule
*/
void loadSchedule() {
	File f = FILESYS.open("/" + schedule, "r");
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
	File f = FILESYS.open("/" + schedule, "w");
	f.print("#Weekly Schedule\n");
	f.print("#Each line is eventCode(0-255),day(1-7),hour(0-23),minute(0-59),second(0-59)\n");
	wkSchedule.saveEvents(f);
	f.close();
}

/*
  Load config
*/
void loadConfig() {
	String line = "";
	int config = 0;
	File f = FILESYS.open(CONFIG_FILE, "r");
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
	File f = FILESYS.open(CONFIG_FILE, "w");
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
		loadConfig();
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
	File f = FILESYS.open(NEXT_WATER_FILE, "r");
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
	File f = FILESYS.open(NEXT_WATER_FILE, "w");
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
  getStatus
*/
void getStatus() {
	String response;
	currentTime = now();
	int cDay = weekday(currentTime);
	int cHour = hour(currentTime);
	int cMinute = minute(currentTime);
	int cSecond = second(currentTime);
	
	File f = FILESYS.open("/" + schedule, "r");
	if(f) {
		response = "Time," + NTP.getDateStr(currentTime) + " " + NTP.getTimeStr(currentTime) + "<BR>";
		response += "Day Number," + String(cDay) + "<BR>";
		if (server.arg("load").toInt()) {
			wkSchedule.clearEvents();
			response += "Loaded events," + String(wkSchedule.loadEvents(f)) + "<BR>";
		} else {
			response += "Schedule Count," + String(wkSchedule.getEventCount()) + "<BR>";
		}
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

void setupStart() {
	startUpTime = millis();
	digitalWrite(WATER_PIN, 0);
	digitalWrite(MOISTURE_PIN, 1);
	pinMode(WATER_PIN, OUTPUT);
	pinMode(MOISTURE_PIN, OUTPUT);
	pinMode(PUSH_BUTTON1, INPUT_PULLUP);
	pinMode(PUSH_BUTTON2, INPUT_PULLUP);
}

void extraHandlers() {
	server.on("/reloadConfig", reloadConfig);
	server.on("/status", getStatus);
	server.on("/pulse", webPulse);
	server.on("/updateConfig", updateConfig);
	server.on("/addEvent", addEvent);
	server.on("/deleteEvent", deleteEvent);
}
 
void setupEnd() {
	loadSchedule();
	if(digitalRead(PUSH_BUTTON1) == 0) {
		sleepMode = SLEEP_MODE_OFF;
		Serial.println(F("Sleep override active"));
	}
	NTP.begin ("pool.ntp.org", timeZone, true, minutesTimeZone);
	NTP.setInterval(NTP_SHORTINTERVAL, 60);
	attachInterrupt(PUSH_BUTTON2, buttonPushInterrupt, RISING);
	Serial.println(F("Set up complete"));
	Serial.println("Setup ended at " + String(millis()));
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
				sleepuSec = 1e6*sleepInterval;
				ESP.deepSleep(sleepuSec);
			} else {
				sleepuSec = 1e6*(timeTillNext + 30);
				ESP.deepSleep(sleepuSec);
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
