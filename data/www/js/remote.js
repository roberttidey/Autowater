/*
 * Script for communicating with radiator remote control.
 */
var serverUrl = "remote.php";
var deviceName = "core2";
var updatingSchedule = false;
var INVALID_TIME = 1440;

$(document).ready(function() {
   $("[data-toggle=\"tooltip\"]").tooltip(); //todo: Not working
   $("#remote-buttons button").on("click", function() {
      sendMessage($(this).data("code"));
   });
   $("#raw-code button").on("click", function() {
      sendMessage($(this).data("code") + "$" + $("#raw-code-msg").val());
   });
   $("#time-submit").on("click", function() {
      updateSchedule();
   });
   $("#time-add").on("click", function() {
      var $template = $("#time-row-template");
      var $schedule = $("#schedule");
      var index = $(".time-row", $schedule).length - 1; // Number of rows, minus 1 for template
      addRow($schedule, $template, index);
   });
   refresh(5000);
});

function refresh(updateRate) {
   getCurrentSchedule();
   setInterval(function(){ getCurrentStatus(); }, updateRate);
}

function setUpdatingSchedule(value, message) {
    showLoading(value, message);
    updatingSchedule = value;
}

function showLoading(show, message) {
    $("#remote-loading-text").text(message || "");
    var $loading = $("#remote-loading");
    if ($loading.is(":hidden") === show)
    {
        $loading.modal("toggle");
    }
}

function addRow($schedule, $template, index) {
   var $row = $template
      .clone()
      .attr("id", "time-row-" + index)
      .appendTo($schedule);

   $("[data-time=\"delete\"]", $row).on("click", function() {
      $(this).closest(".time-row").remove();
   });
   
   $(".time-onoff [data-time]", $row).timepicker({
      minuteStep: 1,
      template: 'dropdown',
      appendWidgetTo: 'body',
      showSeconds: false,
      showMeridian: false,
      defaultTime: false
   });
   
   return $row;
}

function updateTable(sch) {
   var $template = $("#time-row-template").detach();
   var $schedule = $("#schedule").empty().append($template);
   
   for (var i in sch) {
      var $row = addRow($schedule, $template, i);
      
      // If the on time is greater than 2047 minutes, this means the
      // row should be disabled. The original on time is found by
      // masking out bit 12.
      if (sch[i].on > 2047)
      {
         $row.addClass("time-row-disabled")
         $("[data-time=\"enable\"]", $row).prop("checked", false);
         sch[i].on &= 2047;
         sch[i].off &= 2047;
      }
      else
      {
          $row.removeClass("time-row-disabled");
      }
         
      $("[data-time=\"on\"]", $row)
         .timepicker('setTime', minutesToTime(sch[i].on));
         
      $("[data-time=\"off\"]", $row)
         .timepicker('setTime', minutesToTime(sch[i].off));
         
      for (var j in sch[i].days) {
         $("[value=\"" + sch[i].days[j] + "\"]", $row).closest("label").button('toggle');
      }
   }
}

function getCurrentStatus() {
   var stat="";
   $.get(serverUrl + "?deviceName=" + deviceName + "&cmd=status", {},
      function(str) {
         var array = str.split(',');
         var radiatorOn = (array[0] === "1");
         var eventDay = parseInt(array[1]);
         var eventPeriod = parseInt(array[2]);
         var statusTime = moment(array[3]);
         var temp = parseFloat(array[4]);
         
         // Set status values
         $("#status-onoff").toggleClass("status-on", radiatorOn).prop("title", radiatorOn ? "On" : "Off");
         $("#status-time").text(statusTime.format("ddd D MMM HH:mm"));
         $("#status-temp").text(temp.toFixed(1));
         
         // Highlight the last active event
         // Don't do this if the schedule is currently being read, as the table may be incomplete.
         if (updatingSchedule === false)
         {
             $(".time-onoff").removeClass("active");
             
             // Period 0 means the first event today has not happened yet,
             // so the last event was on the previous day.
             if (eventPeriod == 0) {
                eventDay = (eventDay == 0) ? 6 : eventDay-1;
                eventPeriod = 7;
             }
             else {
                eventPeriod -= 1;
             }
             
             var periods = getDayPeriods(eventDay);
             var selectedPeriod = Math.floor(eventPeriod/2);
             var onOff = (eventPeriod%2 == 0) ? "on" : "off";
             
             var $row = periods[selectedPeriod].row;
             $("[data-time=\"" + onOff + "\"]", $row).closest(".time-onoff").addClass("active");
         }
      }
   );
}

function minutesToTime(mins) {
   if (mins > 2047) {
      return "None";
   } else if (mins > 1439) {
      return "Bad";
   } else {
      return ("0" + Math.floor(mins/60)).slice(-2) + ":" + ("0" + mins%60).slice(-2);
   }
}

function timeToMinutes(str, enabled) {
   var d = str.split(':');
   if (d.length == 2) {
      var mins = parseInt(d[0])*60 + parseInt(d[1]);
      if (enabled == false) {
         mins += 2048;
      }
      return mins;
   }
   else {
      return false;
   }
}

function isDayChecked($row, day) {
   var $day = $("[data-time=\"days\"] input[value=\"" + day + "\"]", $row);
   return $day.prop("checked");
}

function getPeriod($row) {
   var enabled = $("[data-time=\"enable\"]", $row).prop("checked");
   var period = {
      on: timeToMinutes($("[data-time=\"on\"]", $row).val(), enabled),
      off: timeToMinutes($("[data-time=\"off\"]", $row).val(), enabled),
      row: $row
   };
   if (period.on < period.off) {
      return period;
   }
   else {
      return false;
   }
}

function sortPeriodsByStartTime(periods) {
    periods.sort(function(a,b) {
        if (a.on < b.on) {
            return -1;
        }
        else if (a.on > b.on) {
            return 1;
        }
        else {
            return 0;
        }
    });
}

function getDayPeriods(day) {
   var periods = [];
      
   // Find all of the periods for one day
   $(".time-row").each(function() {
      var $row = $(this);
      if (isDayChecked($row, day))
      {
         var period = getPeriod($row);
         if (period !== false) {
            periods.push(period);
         }
         else {
            console.log("Invalid time period in day " + day);
         }
      }
   });
      
   // If there is more than one period, make sure the periods are in order
   // from earliest to latest, and there are no overlaps.
   if (periods.length > 1) {
      
      sortPeriodsByStartTime(periods);
      
      // Check for overlaps
      var validPeriods = [];
      validPeriods.push(periods[0]);
      for (p=1; p<periods.length; ++p) {
         var p0 = validPeriods[validPeriods.length-1];
         var p1 = periods[p];
         if (p0.off < p1.on) {
            validPeriods.push(p1);
         }
      }
      periods = validPeriods;
   }
   
   // Make sure there are exactly four periods
   if (periods.length < 4) {
      for (p=periods.length; p<=4; p++) {
         periods.push({
            on: INVALID_TIME,
            off: INVALID_TIME
         });
      }
   }
   else if (periods.length > 4) {
      periods.splice(4);
      console.log("More than four periods for day " + day);
   }
   
   return periods;
}

function getCurrentSchedule() {
    setUpdatingSchedule(true, "Loading...");
   $.get(serverUrl + "?deviceName=" + deviceName + "&cmd=schedule", {},
      function(str) {
         var array = str.split(',');
         var sch = [];
         for (day=0; day<7;day++) {
            for (period=0;period<4;period++) {
               var index = 8*day+2*period;
               var timeOn = parseInt(array[index]);
               var timeOff = parseInt(array[index+1]);
               if (timeOn != INVALID_TIME && timeOff != INVALID_TIME) {
                  var found = false;
                  for (i in sch) {
                     if (sch[i].on == timeOn && sch[i].off == timeOff) {
                        found = i;
                        break;
                     }
                  }
                  if (found === false) {
                     sch.push({
                        on: timeOn,
                        off: timeOff,
                        days: [day]
                     });
                  }
                  else {
                     sch[found].days.push(day);
                  }
               }
            }
         }
         
         sortPeriodsByStartTime(sch);
         updateTable(sch);
         setUpdatingSchedule(false);
         getCurrentStatus();
      }
   );
}

function updateSchedule() {
   setUpdatingSchedule(true, "Saving...");
    
   var sch = [];
   for (day=0; day<7; ++day) {
      var periods = getDayPeriods(day);
      var arr = [day];
      for (p=0; p<4; ++p) {
         arr.push(periods[p].on);
         arr.push(periods[p].off);
      }
      sch.push(arr.join(","));
   }

   $.post(serverUrl + "?deviceName=" + deviceName + "&cmd=receiveSch",
      { params: sch.join(":") },
      function() { getCurrentSchedule(); }
   );
}

function sendMessage(msg) {
   $.post( serverUrl + "?deviceName=" + deviceName + "&cmd=sendMsg", { params: msg });
}