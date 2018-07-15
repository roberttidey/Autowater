/*
  wkEvents.h - Library for structuring events on a weekly basis
  Created by Robert Tidey, July 2, 2018.
  Released into the public domain.
*/
#ifndef wkEvents_h
#define wkEvents_h
#include "Arduino.h"

#define MAX_EVENTS 128
#define EVENT_MASK 0xFF
#define EVENT_SHIFT 8
#define SECONDSPERWEEK 604800

class wkEvents
{
  public:
    wkEvents();
    void clearEvents();
	int getEventCount();
    int loadEvents(Stream &streamEvents);//returns eventCount
    int saveEvents(Stream &streamEvents);//returns eventCount
    int addEvent(int eventCode, int iDay, int iHour, int iMinute = 0, int iSecond = 0);//returns eventIndex or -1
    int deleteEvent(int iDay, int iHour, int iMinute = 0, int iSecond = 0);//returns eventIndex or -1
    int getEventIndex(int iDay, int iHour, int iMinute = 0, int iSecond = 0);//returns index
	int getSecondsToNext(int iDay, int iHour, int iMinute = 0, int iSecond = 0); // returns period to next event
    int getEventCode(int index);//returns code for an index
    int getEventCode(int iDay, int iHour, int iMinute = 0, int iSecond = 0);//returns eventCode for period during time or -1
    int getEventSeconds(int index);//returns iSeconds for an index
	String getEventString(int eventIndex);//returns eventString or ""
    int checkChange(int iDay, int iHour, int iMinute = 0, int iSecond = 0);//returns eventCode if different period to last time called or -1
	int getWeekSeconds(int iDay, int iHour, int iMinute = 0, int iSecond = 0);//week iSeconds
  private:
    int _events[MAX_EVENTS];
	int _lastEventIndex;
	int findIndex(int event);
};

#endif