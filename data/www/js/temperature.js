/*
 * Script for communicating with radiator remote control.
 */
var serverUrl = "remote.php";
var deviceName = "core1";
var PLOT_SECS = 3600;

var minT = 100.0;
var maxT = 0.0;
var sumT = 0.0;
var numR = 0.0;
var lastT = 0.0;
var firstTime = 0.0;
var onTime = Date.now();
var offTime = Date.now();
var boilerState = "Unknown";
var onDuration = 0.0;
var offDuration = 0.0;
var onTotal = 0.1;
var offTotal = 0.1;
var plotData = [];
var i = 0;

for (i = 0; i<PLOT_SECS; i+=5) {
	plotData.push([0,0]);
}


$(document).ready(function() {
	$("[data-toggle=\"tooltip\"]").tooltip(); //todo: Not working
	refresh(5000);
});

function refresh(updateRate) {
	setInterval(function(){ getCurrentStatus(); }, updateRate);
}

function getCurrentStatus() {
	var stat="";
	$.get(serverUrl + "?deviceName=" + deviceName + "&cmd=status", {},
		function(str) {
			var array = str.split(',');
			var statusTime = moment(array[3]);
			var statusmsec = Date.parse(array[3]);
			var currT = parseFloat(array[4]);
			var radiatorOn = 0;
			var cSecs = 0;
			
			if (currT > 0 && currT < 100) {
				// Set status values
				$("#status-time").text(statusTime.format("ddd D MMM HH:mm:ss"));
				$("#status-temp").text(currT.toFixed(1));
				plotData.shift();
				cSecs = (statusmsec - firstTime) / 1000;
				plotData.push([cSecs,currT]);
				processTemp(currT, statusmsec);
				radiatorOn = (boilerState == "On");
				$("#status-onoff").toggleClass("status-on", radiatorOn).prop("title", radiatorOn ? "On" : "Off");
				$.plot($("#tempChart"), [plotData], {series: { lines: { show: false}, points: {show: true }}, points:{ radius: 1}, yaxis: { min: 20, max: 80 }, xaxis: { min: cSecs-PLOT_SECS, max: cSecs }});
			}
		}
	);
}

function processTemp(currT, statusmsec) {
	var aveT = 0;
	var bState = "";

	if (currT < minT) minT = currT;
	if (currT > maxT) maxT = currT;
	numR++;
	sumT += currT;
	aveT = sumT / numR;
	bState = boilerState;
	if (lastT > 0 ) {
		if ((currT - lastT) > 0.02) {
			bState = "On";
		} else if((lastT - currT) > 0.02) {
			bState = "Off";
		}
		if (bState != "Unknown" && bState != boilerState) {
			if (bState == "On") {
				$("#value-trough").text(lastT.toFixed(1));
				onTime = statusmsec;
				offDuration = (onTime - offTime) / 1000;
				offTotal += offDuration;
			}
			if (bState == "Off") {
				$("#value-peak").text(lastT.toFixed(1));
				offTime = statusmsec;
				onDuration = (offTime - onTime) / 1000;
				onTotal += onDuration;
			}
		}
		if (bState != "Unknown" ) boilerState = bState;
	}
	if (firstTime == 0.0) firstTime = statusmsec;
	$("#value-minimum").text(minT.toFixed(1));
	$("#value-maximum").text(maxT.toFixed(1));
	$("#value-average").text(aveT.toFixed(1));
	$("#value-lastoff").text(offDuration.toFixed(0));
	$("#value-laston").text(onDuration.toFixed(0));
	$("#value-percentage").text(Number(onTotal/(onTotal + offTotal)*100).toFixed(1));
	$("#value-duration").text(Number((statusmsec-firstTime)/1000).toFixed(0));
	lastT = currT;
}


