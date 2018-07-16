# AutoPlantWater
Auto Plant watering module for esp8266

Construction details at https://www.instructables.com/id/Battery-Powered-Plant-Watering/

## Features
- Neat self contained unit with reservoir and attached electronics
- Battery powered (rechargeable LIPO) with inbuilt charger
- Very low quiescent current (< 20uA) for long battery life
- Efficient pump only turned on for short periods again giving good battery life.
- 3 watering modes
- Regular intervals
- Automatic based on soil moisture sensing
- Weekly schedule
- Normally the module sleeps between watering times but can be turned into a non sleep mode for checking and configuration
- Schedule, status and configuration data from web interface​
- Software can be updated via web interface
- Low cost

## Library details - wkEvents
This uses a simple library to organise events on a weekly schedule.
Each event has a time consisting of day,hour,minute and second, together with an event number 0-255
Any number of events can be set up.
To use add the library and then declare an instance of the wkEvents class
wkEvents wkSchedule;

The following calls amy be made to this object
- loadEvents(Stream &streamEvents) - load events from a stream like an open file
- saveEvents(Stream &streamEvents) - save events to a stream like an open file
- clearEvents - clear out events from schedule, 1 is left midnight on 1st day event 0
- getEventCount - returns count of events in the schedule
- addEvent(int eventCode, int iDay, int iHour, int iMinute, int iSecond) - add 1 new event or replace existing one
- deleteEvent(int iDay, int iHour, int iMinute = 0, int iSecond = 0) - delete an existing event
- getEventIndex(int iDay, int iHour, int iMinute = 0, int iSecond = 0) - returns index of matching event or -1 if not found
- getSecondsToNext(int iDay, int iHour, int iMinute = 0, int iSecond = 0) - returns seconds to next event
- getEventCode(int index) - returns event code for an index
- getEventCode(int iDay, int iHour, int iMinute = 0, int iSecond = 0) - returns eventCode for a time
- getEventSeconds(int index) - returns seconds for an index
- getEventString(int eventIndex) - returns CSV eventString for an index
- checkChange(int iDay, int iHour, int iMinute, int iSecond) - returns eventCode if different period to last time called or -1
- getWeekSeconds(int iDay, int iHour, int iMinute, int iSecond) - returns seconds for a specified time





