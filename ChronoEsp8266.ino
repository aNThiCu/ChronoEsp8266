#include "config.h"
#include <ESPAsyncWebServer.h>

#include <AsyncElegantOTA.h>
#include <WebSerial.h>

#include <DNSServer.h>

#include <Arduino_JSON.h>

//serverSettings
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 5, 5);

DNSServer dnsServer;
AsyncWebServer webServer(80);

String getData();

//for debug purposes(unsigned long int prints)
void bigPrint(uint64_t n){
  unsigned char temp;
  String result=""; //Start with a blank string
  if(n==0){WebSerial.println(0);return;} //Catch the zero case
  while(n){
    temp = n % 10;
    result=String(temp)+result; //Add this digit to the left of the string
    n=(n-temp)/10;      
    }//while
    WebSerial.println(result);
  }


//server code
const char responseHTML[] PROGMEM = R"rawliteral(<!DOCTYPE html> 
<html> 
<head>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<style> 

	.flex-container {
    display: flex;
	justify-content: center;
	}

	.flex-child {
    flex: 1;
	min-width:0;
	min-height:0;
    border: 5px solid #B87333;
	font-size: 2.5vw;
	}  

	.flex-child:first-child {
    margin-right: 20px;
	} 
	
	.container{
	border: 5px solid #B87333;
	}
	
	.value{
	font-size:100px;
	}
	
	rect{
	fill:#111111;
	stroke:#B87333;
	stroke-width:4px;
	rx:5px;
	}
	
	body {text-align: center; background-color:#111111 ; margin: 20px auto;}
	
	#dataVals { display: block; margin-top: 30px; color:#B87333;}
	
</style>
</head> 
<body>
<div class="flex-container">
<div id="dataVals" class="flex-child"> 
 <h1>Speed(m/s)</h1>
 <div class="value" id="ms"></div>
</div> 

<div id="dataVals" class="flex-child">
 <h1>Energy(Joules)</h1> 
 <div class="value" id="j"> </div>
</div>
</div>

<div id="dataVals" style="border:5px solid #B87333">
<h1>RPS</h1>
<div class="value" id="rps"></div>
</div>

<h1 id="dataVals">Speed Chart</h1>
<svg id="bar-js" ></svg>

<div class="flex-container">

<div id="dataVals" class="flex-child"> 
 <h1>Sensor1</h1>
 <div class="value" id="s1"></div>
</div> 

<div id="dataVals" class="flex-child">
 <h1>Sensor2</h1> 
 <div class="value" id="s2"> </div>
</div>
</div>

<script> 
	
	var dataset = [];
	var counter=0;
	var lastVal=0;
	var svgWidth = 350 , svgHeight = 250, barPadding = 5;
	

	var svg = document.getElementById('bar-js');
	svg.setAttribute("width", svgWidth);
	svg.setAttribute("height", svgHeight);
	
	function loadChart(){
	//resetSvg so old shapes dont overflow
	svg.innerHTML="";
	var barWidth = (svgWidth / dataset.length);
	for(var i = 0; i < dataset.length; i++){
		//textAttr
	var txt = document.createElementNS("http://www.w3.org/2000/svg", "text");
		txt.textContent = dataset[i];
		txt.setAttributeNS(null,"x",barWidth*i+barWidth/2-5);
		txt.setAttributeNS(null,"y",svgHeight - (dataset[i]/2));
		txt.setAttributeNS(null,"dominant-baseline","middle");
		txt.setAttributeNS(null,"text-anchor","middle");
		txt.setAttributeNS(null,"font-size","40");
		txt.setAttributeNS(null,"fill","#B87333");
		//shapeAttr
	var rect = document.createElementNS("http://www.w3.org/2000/svg", "rect");
		rect.setAttribute("y", svgHeight - dataset[i]);
		rect.setAttribute("height", dataset[i]);
		rect.setAttribute("width", barWidth-barPadding);
		//combine
	var translate = [barWidth * i, 0];
		rect.setAttribute("transform", "translate("+ translate +")");
	svg.appendChild(rect);
	svg.appendChild(txt);
	}
	}
	
	
	function loadDoc() { 
		var xhttp = new XMLHttpRequest(); xhttp.onreadystatechange = function() { 
			if (this.readyState == 4 && this.status == 200) { 
				var obj = JSON.parse(this.responseText); 
				document.getElementById("ms").innerHTML = obj.Metric;
				if(lastVal!=obj.Metric)
				{	
					lastVal=obj.Metric;
					getChartData(lastVal);
					loadChart();
				}
				document.getElementById("j").innerHTML = obj.Joules;
				document.getElementById("rps").innerHTML = obj.RPS;
				document.getElementById("s1").innerHTML = obj.sens1;
				document.getElementById("s2").innerHTML = obj.sens2;
			}
		}; 
		
		xhttp.open("GET", "/data", true); xhttp.send(); 
	} 
	
	function getChartData(parsedValue)
	{
		if(counter<5)
		dataset[counter++]=parsedValue;
		else
		{
		for(var j=0;j<counter-1;j++)
			dataset[j]=dataset[j+1];
		dataset[counter-1]=parsedValue;
		}
	}
	
	var timedEvent = setInterval(function(){loadDoc()}, 200); 
	</script> 
	</body> 
	</html>
)rawliteral";

class CaptiveRequestHandler : public AsyncWebHandler {
public:
  CaptiveRequestHandler() {

    webServer.on("/data", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(200, "text/plain", getData());
    });

    webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(200, "text/html", responseHTML);
    });

    WebSerial.begin(&webServer);
    AsyncElegantOTA.begin(&webServer);
  }
  virtual ~CaptiveRequestHandler() {}

  bool canHandle(AsyncWebServerRequest *request) {
    return true;
  }

  void handleRequest(AsyncWebServerRequest *request) {

    request->send(200, "text/html", responseHTML);
  }
};

//Measuring
float metric = 0;
float joules = 0;
float rps = 0;
//sensorTimeGet
unsigned long int fp = 0;
unsigned long int lp = 0;
//sensorChecks
bool firstSensorTrigger=false;
//first and last timeValues
unsigned long int firstShot = 0;
unsigned long int lastShot = 0;
//rpsMaxTimeBetweenShots
int shotCount = 0;
bool startedRpsCounting = false;
bool bbPassedSensor=true;

void calculate() {
  unsigned long int microDif=0;
  microDif = lp - fp;
  metric = (SENSOR_DISTANCE / 1000.0) / (microDif / 1000000.0);  //mm->m us->s
  joules = (((BB_WEIGHT / 1000.0) / 2) * (metric * metric));
  
  WebSerial.println("Measurments");
  WebSerial.println(metric);
  WebSerial.println(joules);
  WebSerial.println("Time of detection:");
  WebSerial.print("Sensor1:");
  bigPrint(fp);
  WebSerial.print("Sensor2:");
  bigPrint(lp);

  fp = 0;
  lp = 0;
}

void IRAM_ATTR getSensor1(){
  //getSpeedValue
  if(firstSensorTrigger==false) 
  {
    fp=ESP.getCycleCount()/160;
    firstSensorTrigger=true;
  }
  //makeRpsCalculation
  if (startedRpsCounting == false && bbPassedSensor == true) {
      firstShot = ESP.getCycleCount()/160;
      lastShot= ESP.getCycleCount()/160;
      startedRpsCounting = true;
      shotCount++;
      rps = 0;
      bbPassedSensor = false;
    } 
    else if(startedRpsCounting == true && bbPassedSensor == true)
    {
      shotCount++;
      lastShot = ESP.getCycleCount()/160;
      rps = shotCount / ((lastShot - firstShot) / 1000000.0);
      bbPassedSensor = false;
    }
  }

void IRAM_ATTR getSensor2(){
  if(firstSensorTrigger==true)
  {
    lp=ESP.getCycleCount()/160;
    firstSensorTrigger=false;
  }
}

String getData() {

  JSONVar data;
  char value1[20];
  char value2[20];
  char value3[20];

  dtostrf(metric, 1, 2, value1);
  dtostrf(joules, 1, 2, value2);
  dtostrf(rps, 1, 2, value3);

  data["Metric"] = value1;
  data["Joules"] = value2;
  data["RPS"] = value3;
  data["sens1"] = digitalRead(PT1);
  data["sens2"] = digitalRead(PT2);
  String text = JSON.stringify(data);
  return text;
}

void setup() {
  delay(5000);

  pinMode(PT1, INPUT);
  pinMode(PT2, INPUT);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP("ChronoGraph");

  dnsServer.start(DNS_PORT, "*", apIP);
  webServer.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);

  webServer.begin();

  attachInterrupt(digitalPinToInterrupt(PT1), getSensor1, FALLING);
  attachInterrupt(digitalPinToInterrupt(PT2), getSensor2, FALLING);
}

void loop() {
  dnsServer.processNextRequest();

  if ((fp != 0 && lp != 0) && (digitalRead(PT2) == 1 && digitalRead(PT1) == 1))
  calculate();

  if(bbPassedSensor == false && digitalRead(PT1) == 1)
  bbPassedSensor = true;

  if (micros() - lastShot > 500000 && startedRpsCounting == true) 
  {
    startedRpsCounting = false;
    shotCount = 0;
  }
}