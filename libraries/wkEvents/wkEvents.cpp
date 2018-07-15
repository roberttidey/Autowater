/*
  wkEvents.cpp - Library for structuring events on a weekly basis
  Created by Robert Tidey, July 2, 2018.
  Released into the public domain.
*/

#include "Arduino.h"
#include "wkEvents.h"

wkEvents::wkEvents() {
	clearEvents();
}

void wkEvents::clearEvents() {
	int i;
	for(i=0;i<MAX_EVENTS;i++) {
		_events[i] = -1;
	}
	_events[0] = 0;
}

int wkEvents::getEventCount() {
	int i;
	for(i=0;i<MAX_EVENTS;i++) {
		if(_events[i] == -1) break;
	}
	return i;
}

int wkEvents::loadEvents(Stream &streamEvents) {
	String line;
	String field;
	int fields[5];
	int i,j,f;
	while(streamEvents.available()) {
		line = streamEvents.readStringUntil('\n');
		line.replace("\r","");
		if(line.length() > 0 && line.charAt(0) != '#') {
			i=0;
			j=0;
			for(f=0;f<5;f++) fields[f] = 0;
			for(f=0;f<5;f++) {
				j=line.indexOf(',',i);
				if(j<0) {
					field = line.substring(i).toInt();
				} else {
					field = line.substring(i,j).toInt();
				}
				if(field.length() > 0) {
					fields[f] = field.toInt();
				}
				if(j<0) break;
				i = j+1;
			}
			addEvent(fields[0],fields[1],fields[2],fields[3],fields[4]);
		}
	}
	return getEventCount();
}

// Save events to Stream
int wkEvents::saveEvents(Stream &streamEvents) {
	int i;
	int j = getEventCount();
	for(i=1;i<j;i++) streamEvents.print(getEventString(i) + "\n");
	return j;
}

//add event in ordered list, return its index if Ok, or -1 if full
int wkEvents::addEvent(int eventCode, int iDay, int iHour, int iMinute, int iSecond) {
	int s = getWeekSeconds(iDay,iHour,iMinute,iSecond);
	int i = findIndex(s);
	int j,k;
	
	if(eventCode > EVENT_MASK || eventCode < 0) eventCode = 0;
	s = s << EVENT_SHIFT;
	for(i=0;i<MAX_EVENTS;i++) {
		if((_events[i] < 0) || (_events[i] & ~EVENT_MASK) == s) {
			_events[i] = s + eventCode;
			break;
		} else {
			if(_events[i] > s) {
				if(_events[MAX_EVENTS-1] != -1) return -1;
				for(j=i;j<MAX_EVENTS;j++) {
					if(_events[j] < 0) break;
				}
				for(k=j;k>i;k--) {
					_events[k] = _events[k-1];
				}
				_events[i] = s + eventCode;
				break;
			}
		} 
	}
	return i;
}

//delete event if it exists and return its previous index
//return -1 if no matching event
int wkEvents::deleteEvent(int iDay, int iHour, int iMinute, int iSecond) {
	int i = findIndex(getWeekSeconds(iDay,iHour,iMinute,iSecond));
	Serial.println("Delete Index" + String(i));
	int j;
	if(i< 0) {
		i = -i;
		for(j=i;j<MAX_EVENTS-1;j++) {
			_events[j] = _events[j+1];
			if(_events[j+1] < 0) break;
		}
		_events[MAX_EVENTS-1] = -1;
		return -i;
	}
	return -1;
}

int wkEvents::getEventIndex(int iDay, int iHour, int iMinute, int iSecond) {
	int s = getWeekSeconds(iDay,iHour,iMinute,iSecond);
	int i = findIndex(s);
	if(i<0) i = -i;
	return i;
}

int wkEvents::getSecondsToNext(int iDay, int iHour, int iMinute, int iSecond) {
	int currentIndex = getEventIndex(iDay, iHour, iMinute, iSecond);
	int nextIndex = currentIndex + 1;;
	int secondsToNext;
	if(nextIndex >= getEventCount()) nextIndex = 0;
	int currentSeconds = getWeekSeconds(iDay, iHour, iMinute, iSecond);
	int nextSeconds = _events[nextIndex] >> EVENT_SHIFT;
	if(nextSeconds >= currentSeconds) {
		secondsToNext = (nextSeconds - currentSeconds);
	} else {
		secondsToNext = (nextSeconds + SECONDSPERWEEK - currentSeconds);
	}
	return secondsToNext;
}

int wkEvents::getEventCode(int index) {
	if(index < 0 || index > (MAX_EVENTS-1)) {
		return -1;
	} else {
		return _events[index] & EVENT_MASK;
	}
}

int wkEvents::getEventCode(int iDay, int iHour, int iMinute, int iSecond) {
	int i = findIndex(getWeekSeconds(iDay,iHour,iMinute,iSecond));
	if(i > (MAX_EVENTS-1)) return -1;
	if (i< 0) i = -i;
	if(i>0) i--;
	return _events[i] & EVENT_MASK;
}

int wkEvents::getEventSeconds(int index) {
	if(index < 0 || index > (MAX_EVENTS-1)) {
		return -1;
	} else {
		return _events[index] >> 8;
	}
}

String wkEvents::getEventString(int eventIndex) {
	int event,eventCode,iDay,iHour,iMinute,iSecond;
	if(eventIndex >= 0 && eventIndex < MAX_EVENTS) {
		event = _events[eventIndex];
		if(event < 0) return "Null event";
		eventCode = event & EVENT_MASK;
		event = event >> EVENT_SHIFT;
		iSecond = event % 60; event = event / 60;
		iMinute = event % 60; event = event / 60;
		iHour = event % 24; event = event / 24;
		iDay = event % 7;
		return String(eventCode) + "," + String(iDay+1) + "," + String(iHour) + "," + String(iMinute) + "," + String(iSecond); 
	}
	return "";
}

int wkEvents::checkChange(int iDay, int iHour, int iMinute, int iSecond) {
	int i = findIndex(getWeekSeconds(iDay,iHour,iMinute,iSecond));
	if(i == MAX_EVENTS) return -1;
	if (i< 0) i = -i;
	if(i>0) i--;
	if(i == _lastEventIndex) return -1;
	_lastEventIndex = i;
	return _events[i] & EVENT_MASK;
}

int wkEvents::getWeekSeconds(int iDay, int iHour, int iMinute, int iSecond) {
	if(iDay > 7 || iDay < 1) iDay = 1;
	if(iHour > 23 || iHour < 0) iHour = 0;
	if(iMinute > 59 || iMinute < 0) iMinute = 0;
	if(iSecond > 59 || iSecond < 0) iSecond = 0;
	return (iSecond + 60 * (iMinute + 60 * (iHour + 24 * (iDay-1))));
}

//Private support routines

//find event where search criterion equals or exceeds
//return negative value if equal//return MAX_EVENTS if beyond the max
int wkEvents::findIndex(int seconds) {
	int i;
	int search = seconds << 8;
	int diff;
	for(i=0;i<MAX_EVENTS;i++) {
		diff = (_events[i]  & ~EVENT_MASK) - search;
		if((diff > 0) || (_events[i] < 0)) {
			return i-1;
		} else {
			if(diff == 0) {
				return -i;
			}
		}
	}
	return MAX_EVENTS;
}
