/*Alternative firmware for iSpindle hydrometer/gravitation meter
   SPECS:
   Cloud service: Cayenne My Devices
   WifiManager on double reset
   Deep Sleep: Working
   Starting Gravity? and ABV
   Custom Pages: Censor readings, Offset Calibration
   Firmware Update (Only if Latest WiFimanager library from Github is used
   Start WiFiManager configuration portal during brew with a magnet. Use the magnet to draw the iSpindle vertical against the fermenter wal

   LED INDICATOR
   1 second = iSpindle awake after deep sleep
   20 short flashes, one 1 second flas = iSpindle will go into ACCESS POINT mode.
   
   3 short flashes= Data was send to Cayenne cloud.

   TO DO: 
   

   Dev by JJ Slabbert. https://www.instructables.com/member/JJ%20Slabbert/instructables/ and https://github.com/JJSlabbert
   I am not a profesional programmer or engineer. This is my hobby.
   I wrote this firmware, because
   1) The standard iSpindle firmware does not support Cayenne
   2) The firmware source code is to complicated for me to understand. The original Firmware is however great.
   3) The original firmware does not publish ABV
   4) One of my iSpindles MPU6050 Gyro have a Device ID problem and it could not show the Tilt in WiFiManager, althought it could publish the tilt to UBIDOTS
      https://forum.arduino.cc/t/mpu6050-connection-and-acceleration-offset-failure/651690 and https://github.com/universam1/iSpindel/issues/429
*/




//WifiManager global declerations
#include <FS.h>          // this needs to be first, or it all crashes and burns...
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager Install from Github
#include <ArduinoJson.h> // https://github.com/bblanchon/ArduinoJson Use version 5, Install from Arduino IDE

#ifdef ESP32
#include <SPIFFS.h>
#endif

WiFiManager wm;
unsigned int  timeout   = 120; // seconds to run for
unsigned int  startTime = millis();
bool portalRunning      = false;
bool startAP            = false; // start AP and webserver if true, else start only webserver

//define your default values here, if there are different values in config.json, they are overwritten.
char portalTimeOut[5] = "9000";
char mqtt_username[42]; //Cayenne details
char mqtt_password[42];
char mqtt_clientid[43];
char coefficientx3[16] = "0.0000010000000"; //model to convert tilt to gravity
char coefficientx2[16] = "-0.000131373000";
char coefficientx1[16] = "0.0069679520000";
char constantterm[16] = "0.8923835598800";
char batconvfact[9] = "199.285"; //converting reading to battery voltage
char pubint[7] = "0"; // publication interval in seconds
char originalgravity[6] = "1.05";
char tiltOffset[6] = "0";

float fpubint;


//flag for saving data
bool shouldSaveConfig = true;//This was false

//Cayenne global declerations
#define CAYENNE_PRINT Serial     // Comment this out to disable prints and save space
#ifdef ESP32
#include <CayenneMQTTESP32.h> 
#include <ESP32Ping.h>
#endif
#ifdef ESP8266
#include <CayenneMQTTESP8266.h>
#include <ESP8266Ping.h>  
#endif

bool cayenneConnected=false;

//determine board type and DS18B20 global decleration
#include <OneWire.h>  //Instal from Arduino IDE
#include <DallasTemperature.h> //Instal from Arduino IDE
#ifdef ESP8266
#define ONE_WIRE_BUS D6
#endif
#ifdef ESP32
#define ONE_WIRE_BUS 13
#endif
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
//


#ifdef __cplusplus
extern "C" {
#endif
uint8_t temprature_sens_read();
#ifdef __cplusplus
}
#endif
uint8_t temprature_sens_read();


//MPU6050 global decleration
#include <MPU6050.h> //Instal from Arduino IDE
#include <Wire.h>
#ifdef ESP8266
#define SDA_PIN D3
#define  SCL_PIN D4
#endif
#ifdef ESP32
#define SDA_PIN 0//??????????????????????????????????????????????????????????????????????????????????????????????????????????
#define  SCL_PIN 4//??????????????????????????????????????????????????????????????????????????????????????????????????????????
#endif
int i2c_address=0x68;
MPU6050 accelgyro(i2c_address);
int16_t ax, ay, az;

//Other global declerations
#include <curveFitting.h>  //used for polynomial calibration


String htmlMenueText="<a href='/wifi?' class='button'>CONFIGURE WIFI</a>\
<br><a href='/readings?' class='button'>SENSOR READINGS</a>\
<br><a href='/offsetcalibration?' class='button'>OFFSET CALIBRATION</a>\    
<br><a href='/polynomialcalibrationstart?' class='button'>POLYNOMIAL CALIBRATION</a>\
<br><a href='/info?' class='button'>INFO</a>\ 
<br><a href='/exit?' class='button'>EXIT</a>\  
<br><a href='/update?' class='button'>FIRMWARE UPDATE</a>"; 

String htmlStyleText= "<style>  body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088;  max-width:900px; align-content:center;} p {font-size: 30px;} h1 {text-align: center;} .button {  background-color: blue;  border: none;  color: white;  padding: 30px 15px;  text-align: center;  text-decoration: none;  display: inline-block;  font-size: 30px;  margin: 4px 2px;  cursor: pointer; width: 900px;} </style>";

  
void startDeepSleep()
{
  fpubint = atof(pubint);

  if (fpubint <60)
  {
    return;
  } 
  #ifdef ESP32
    delay(2000);
  #endif
  Serial.println("Entering deep sleep for " + String(fpubint) + " seconds");
  ESP.deepSleep(fpubint * 1000000);
  delay(1000);
}

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

float calcOffset()
{ 
  float reading;
  float areading=0;
  float n=100;

  pinMode(SDA_PIN, OUTPUT);//This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  pinMode(SCL_PIN, OUTPUT); //This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  digitalWrite(SDA_PIN, LOW); //This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  digitalWrite(SCL_PIN, LOW);  //This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  delay(100);
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.beginTransmission(i2c_address);
  Wire.write(0x6B);  // PWR_MGMT_1 register
  Wire.write(0);     // set to zero (wakes up the MPU-6050)
  Wire.endTransmission(true);
  accelgyro.initialize();
  for (int i=0; i<n; i++)
  {
    accelgyro.getAcceleration(&ax, &az, &ay);
    reading=acos(az / (sqrt(ax * ax + ay * ay + az * az))) * 180.0 / M_PI;
    areading+=(1/n)*reading;
    Serial.println("Calibrating: "+String(reading));
    Serial.println("Avarage Reading: "+String(areading));
  }
  float offset=89.0-areading;
  return offset;
}

float calcTemp()
  {
    sensors.requestTemperatures();
    return sensors.getTempCByIndex(0);
  }


float calcTilt()
{ 
  float reading;
  float areading=0;
  float n=100;

  pinMode(SDA_PIN, OUTPUT);//This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  pinMode(SCL_PIN, OUTPUT); //This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  digitalWrite(SDA_PIN, LOW); //This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  digitalWrite(SCL_PIN, LOW);  //This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  delay(100);
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.beginTransmission(i2c_address);
  Wire.write(0x6B);  // PWR_MGMT_1 register
  Wire.write(0);     // set to zero (wakes up the MPU-6050)
  Wire.endTransmission(true);
 
  accelgyro.initialize();

  for (int i=0; i<n; i++)
  {
    accelgyro.getAcceleration(&ax, &az, &ay);
    reading=acos(az / (sqrt(ax * ax + ay * ay + az * az))) * 180.0 / M_PI;
    areading+=(1/n)*reading;
//    Serial.println("Calibrating: "+String(reading));
//    Serial.println("Avarage Reading: "+String(areading));
  }
  areading+=atof(tiltOffset);
//  Serial.println("Gyro Readings: "+String(ax)+","+String(ay)+","+String(az));
//  Serial.println("Calculated Tilt: "+String(reading));
  return areading;

}
float calcGrav()
{
  float tilt = calcTilt();
  float fcoefficientx3 = atof(coefficientx3);
  float fcoefficientx2 = atof(coefficientx2);
  float fcoefficientx1 = atof(coefficientx1);
  float fconstantterm = atof(constantterm);
  return fcoefficientx3 * (tilt * tilt * tilt) + fcoefficientx2 * tilt * tilt + fcoefficientx1 * tilt + fconstantterm;
}

float calcABV()
{
  float abv = 131.258 * (atof(originalgravity) - calcGrav());
  return abv;
}

void setupSpiffs() {
  //clean FS, for testing
  //SPIFFS.format();
  //wm.resetSettings();
  //Serial.println("If selected, WM was reset and SPIFFS was formated++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++");
  //  delay(100);
  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");
          strcpy(portalTimeOut, json["portalTimeOut"]);
          strcpy(mqtt_username, json["mqtt_username"]);
          strcpy(mqtt_password, json["mqtt_password"]);
          strcpy(mqtt_clientid, json["mqtt_clientid"]);
          strcpy(coefficientx3, json["coefficientx3"]);
          strcpy(coefficientx2, json["coefficientx2"]);
          strcpy(coefficientx1, json["coefficientx1"]);
          strcpy(constantterm, json["constantterm"]);
          strcpy(batconvfact, json["batconvfact"]);
          strcpy(pubint, json["pubint"]);
          strcpy(originalgravity, json["originalgravity"]);
          strcpy(tiltOffset, json["tiltOffset"]);


          // if(json["ip"]) {
          //   Serial.println("setting custom ip from config");
          //   strcpy(static_ip, json["ip"]);
          //   strcpy(static_gw, json["gateway"]);
          //   strcpy(static_sn, json["subnet"]);
          //   Serial.println(static_ip);
          // } else {
          //   Serial.println("no custom ip in config");
          // }

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read
}

void bindServerCallback() {

  wm.server->on("/readings", handleReadings);
  wm.server->on("/offsetcalibration", handleOffsetCalibration);
  wm.server->on("/polynomialcalibrationstart", handlePolynomialCalibrationStart);
  wm.server->on("/polynomialcalibrationinput",handlePolynomialCalibrationInput);
  wm.server->on("/polynomialcalibrationresults",handlePolynomialCalibrationResults);
  wm.server->on("/",handleRoute); // you can override wm! main page
}
void handleRoute()
{
  Serial.println("[HTTP] handle Route");
  String htmlText = "<HTML><HEAD><TITLE>iSpindle Main Page</TITLE></HEAD>";
  htmlText+= htmlStyleText;
  htmlText+= "<BODY><h1>CONFIGURATION PORTAL for iSpindel</h1>";
  htmlText+=htmlMenueText;
 
   
  htmlText += "</BODY></HTML>";  
  wm.server->send(200, "text/html", htmlText);
}
void handleOffsetCalibration()
{ 
  float offset=calcOffset(); 
  Serial.println("[HTTP] handle Offset Calibration");
  Serial.println("OFFSET Calculated, Send to /offsetcalibration?: "+String(offset));
  String htmlText = "<HTML><HEAD><TITLE>iSpindle Offset Calibration</TITLE></HEAD>";
  htmlText+= htmlStyleText;
  htmlText += "<BODY><h1>OFFSET CALIBRATION</h1>";
  htmlText+="<p> Calculated Offset: "+String(offset)+"</p>";
  htmlText+="<p> Insert this value, with iets sign in the <a href='/wifi?' target='_blank'>Configure WiFi</a> page.</p>";
  htmlText+=htmlMenueText;
  htmlText += "</BODY></HTML>";
  wm.server->send(200, "text/html", htmlText);
}
void handlePolynomialCalibrationResults()
{
  Serial.println("[HTTP] handle Polynomial Calibration Results");
  Serial.println("Number of server arguments: "+String(wm.server->args()));
  int n=wm.server->args()/2;
  Serial.println("Sample Size in handlePolynomialCalibrationResults() : "+String(n));

  String htmlText = "<HTML><HEAD><TITLE>iSpindle Main Page</TITLE></HEAD>";
  htmlText+= htmlStyleText;
  htmlText += "<BODY><h1>POLYNOMIAL CALIBRATION WIZARD: RESULTS</h1>";

//  for (int i=0; i<wm.server->args()-1;i++)
//      {
//        text+= "Argument "+String(i)+", Argument Name: "+wm.server->argName(i)+", Argument Value: "+String(wm.server->arg(i));
//        text+="<br>";        
//      }

  
  double sampledtilt[n];
  double sampledgrav[n];
  htmlText+="<table border='1'><align='right'><tr><td>TILT VALUES</td><td>GRAVITY VALUES</td></tr>";
  for (int i=0; i<n; i++)
  {
    sampledtilt[i]=atof(wm.server->arg(i).c_str());
    Serial.println("sampled tilt"+String(i)+": "+String(sampledtilt[i]));
    sampledgrav[i]=atof(wm.server->arg(i+n).c_str());
    Serial.println("sampled gravity"+String(i)+": "+String(sampledgrav[i],3)); 
    htmlText+="<tr><td>"+String(sampledtilt[i])+"</td><td>"+String(sampledgrav[i],3)+"</td></tr>";
    
    //htmlText+= "Sampled_Tilt"+String(sampledtilt[i]) +", Sampled_Gravity"+String(sampledgrav[i],3)+"<br>";  
  }
  htmlText+="<align></table>";
  int order = 3;
  double coeffs[order+1];   
  int ret = fitCurve(order, sizeof(sampledgrav)/sizeof(double), sampledtilt, sampledgrav, sizeof(coeffs)/sizeof(double), coeffs);
  if (ret==0)
  {
    Serial.println("Coefficiant of tilt^3: "+String(coeffs[0],15));
    Serial.println("Coefficiant of tilt^2: "+String(coeffs[1],15));
    Serial.println("Coefficiant of tilt^1: "+String(coeffs[2],15));
    Serial.println("Constant Term: "+String(coeffs[3],15));
    htmlText+="<br> COPY AND PASTE THE FOLOWING VALUES TO THE <a href='/wifi?' target='_blank'>CONFIGURE WIFI PAGE</a></br>";
    htmlText+="<br>Coefficiant of tilt^3: "+String(coeffs[0],15)+"<br> Coefficiant of tilt^2: "+String(coeffs[1],15)+"<br> Coefficiant of tilt^1: "+String(coeffs[2],15)+"<br> Constant Term: "+String(coeffs[3],15);
  }
  else
  {
    Serial.println("Failed to calculate Coefficients.");
  }
  //Calculating R^2 Rsquare
  float agrav=0;  //average of gravity
  for (int i=0 ; i<n; i++)
  {
    agrav+=(1/float(n))*sampledgrav[i];
    Serial.println("Calculating agrav: "+String(agrav,5));
  }
  Serial.println("Average Gravity: "+String(agrav,8));
  float SSres=0;
  for (int i=0 ; i<n; i++)
  {
    SSres+=pow((sampledgrav[i]-(coeffs[0]*sampledtilt[i]*sampledtilt[i]*sampledtilt[i]+coeffs[1]*sampledtilt[i]*sampledtilt[i]+coeffs[2]*sampledtilt[i]+coeffs[3])),2);
  }
  Serial.println("SSres: "+String(SSres,8));
  float SStot=0;
  for (int i=0 ; i<n; i++)
  {
    SStot+=pow((sampledgrav[i]-agrav),2);
  }
  Serial.println("SStot: "+String(SStot,8));
  float Rsquare=1-SSres/SStot;
  htmlText+="<br> Coefficient of determination: "+String(Rsquare,15)+" (This measures the strenght of the statistical fit. You should get something in the range of 0.98-0.99"; 
  htmlText+=htmlMenueText; 
  htmlText+="</BODY></HTML>";
  wm.server->send(200, "text/html", htmlText);
  
}

void handlePolynomialCalibrationInput()
{ 
  String htmlText="<HTML><HEAD><TITLE>iSpindle Polynomial Calibration</TITLE></HEAD><style>  body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; } </style>";
  htmlText+= htmlStyleText;
  htmlText += "<BODY><h1>POLYNOMIAL CALIBRATION WIZARD: SAMPLED DATA</h1>";
      htmlText+= "<br><table><tr><td><form action='/polynomialcalibrationresults?' method ='POST'> <br> TILT:";
  int n=atoi(wm.server->arg(0).c_str());
  Serial.println("Sample Size in handlePolynomialCalibrationInput() : "+String(n));
  for (int i=0; i<n; i++)
  {
    htmlText+= "<br><input type='number' name=+""tilt"+String(i) +" size='5' min='20.00' max='80.00' step='0.01'>";
  }

  htmlText+="</td><td>GRAVITY:";
  for (int i=0; i<n; i++)
  {
    htmlText+= "<br><input type='number' name=+""gravity"+String(i) +" size='5' min='1' max='1.12' step='0.001'>";
  }
  htmlText += "</td></tr></table><br><input type='submit' value='Submit'>  </form><br>";
  htmlText += "<br></BODY></HTML>";
  wm.server->send(200, "text/html", htmlText);
}

void handlePolynomialCalibrationStart()
{
  String htmlText = "<HTML><HEAD><TITLE>iSpindle Calibration Page</TITLE></HEAD>";
  htmlText+= htmlStyleText;
  htmlText+= "<BODY><h1>POLYNOMIAL CALIBRATION WIZARD</h1>";
  htmlText+="<p> This links will not work if you are connected to the iSpindel Access point.</p>";
  htmlText+="<p>Sugar Wash calculators will indicate how much sugar you need to add into a specific amount of water to reach certain gravity</p>";
  htmlText+="<p><a href='https://chasethecraft.com/calculators'>Sugar wash Calculator at https://chasethecraft.com/calculators</a></p>";
  htmlText+="<p><a href='https://www.hillbillystills.com/distilling-calculator'>Sugar wash Calculator at https://www.hillbillystills.com/distilling-calculator</a></p>";  
  htmlText+="<p><a href='https://www.ispindel.de/docs/Calibration_en.html#easy-method-(I)'>Methods of Calibration on https://www.ispindel.de/docs/Calibration_en.html#easy-method-(I)</a></p>";
  htmlText+="<p><a href='https://www.ispindel.de/tools/calibration/calibration.htm'>Calibration Calculater on https://www.ispindel.de/tools/calibration/calibration.htm</a></p>"; 
  htmlText+="<p> POLYNOMIAL CALIBRATION WIZARD</p>";  
  htmlText+="<br><br><form action='/polynomialcalibrationinput?' method ='POST'> SAMPLE SIZE (6-30): <input type='number' name='sample_size' size='5' min='6' max='30' step='1'><input type='submit' value='Submit'>  </form><br>";
  //htmlText += "<p><a href='/wifi?'>--Configure Wifi--</a><a href='/readings?'>--Sensor Readings--</a> <a href='/offsetcalibration?'>--Offset Calibration--</a><a href='/polynomialcalibrationstart?'>--Polynomial Calibration--</a><a href='/'>--Main Page--</a></p>";
  htmlText+=htmlMenueText;   
  
  htmlText += "</BODY></HTML>";  
  wm.server->send(200, "text/html", htmlText);    
}

void handleReadings() {
  Serial.println("[HTTP] handle Readings");
  String htmlText = "<HTML><HEAD><meta http-equiv='refresh' content='1'><TITLE>iSpindle Main Page</TITLE></HEAD>";
  htmlText+= htmlStyleText;
  htmlText+= "<BODY><h1>iSpindle Sensor Readings</h1>";
  htmlText+= "<p>Sensor Readings will update every 2 seconds.</p>";
  htmlText+= "<p>Battery Voltage: " + String(analogRead(A0) / atof(batconvfact)) + "</p>";
  htmlText+= "<p>Tilt: " + String(calcTilt()) + " Degrees. This value should be about 89 degrees if the iSpindel is on a horizontal surface.</p>";
  htmlText+= "<p>Gravity: " + String(calcGrav(), 5) + " SG</p>";
  htmlText+= "<p>Temperature: " + String(calcTemp()) + " Degrees Celcius</p>";
  htmlText+= "<p>Alcohol by Volume (ABV): " + String(calcABV()) + " %</p>";
  //htmlText += "<p><a href='/wifi?'>--Configure Wifi--</a><a href='/readings?'>--Sensor Readings--</a> <a href='/offsetcalibration?'>--Offset Calibration--</a><a href='/polynomialcalibrationstart?'>--Polynomial Calibration--</a><a href='/'>--Main Page--</a></p>";
  htmlText+=htmlMenueText; 
  htmlText += "</BODY></HTML>";
  wm.server->send(200, "text/html", htmlText);
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  delay(1000);
  Serial.begin(115200);
  Serial.println("Start setup()");

  setupSpiffs();


  //Setting callbacks
  wm.setWebServerCallback(bindServerCallback);
  wm.setSaveConfigCallback(saveConfigCallback);

  wm.setCustomHeadElement("<font size='6'><b>Configuration for the iSpindle</b></font><br>");


  // setup custom parameters
  //
  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
                                                                   //Custom HTML
  WiFiManagerParameter custom_portalTimeOut("portalTimeOut", "WiFi Manager Configuration Portal Time Out", portalTimeOut, 4);
  WiFiManagerParameter custom_mqttHTML("<p><font size='4'><b>Provide the Parameters of Cayenne</b></font></p>");
  WiFiManagerParameter custom_mqtt_username("usernname", "MQTT USERNAME", mqtt_username, 41);
  WiFiManagerParameter custom_mqtt_password("pasword", "MQTT PASSWORD", mqtt_password, 41);
  WiFiManagerParameter custom_mqtt_clientid("clientid", "MQTT CLIENT ID", mqtt_clientid, 42);
  WiFiManagerParameter custom_polynomialHTML("<p><font size='4'><b>Provide the Parameters of you Polynomial</b></font></p>");                                                      //Custom HTML
  WiFiManagerParameter custom_coefficientx3("coefficientx3", "Coefficient of Tilt^3 (Regression Model Polynomial)", coefficientx3, 15);
  WiFiManagerParameter custom_coefficientx2("coefficientx2", "Coefficient of Tilt^2 (Regression Model Polynomial)", coefficientx2, 15);
  WiFiManagerParameter custom_coefficientx1("coefficientx1", "Coefficient of Tilt^1 (Regression Model Polynomial)", coefficientx1, 15);
  WiFiManagerParameter custom_constantterm("constantterm", "Constant Term (Regression Model Polynomial)", constantterm, 15);
  WiFiManagerParameter custom_ispindleotherHTML("<p><font size='4'><b>Other Parameters for the iSpindle</b></font></p>");                                                           //Custom HTML
  WiFiManagerParameter custom_batconvfact("batconvfact", "Battery Conversion Factor", batconvfact, 8);
  WiFiManagerParameter custom_pubint("pubint", "Data Publication Interval", pubint, 6);
  WiFiManagerParameter custom_originalgravity("originalgravity", "Original Gravity", originalgravity, 5);

  WiFiManagerParameter custom_tiltOffset("tilt_Offset", "Calibrated Tilt Offset", tiltOffset, 5);
  WiFiManagerParameter custom_sensorpage_link("<p><a href='/readings?'>Live sonsor readings</a><p>");
  WiFiManagerParameter custom_offsetcalibration_link("<p><a href='/offsetcalibration?'>Tilt Offset Calibration</a><p>");
  WiFiManagerParameter custom_polynomialcalibration_link("<p><a href='/polynomialcalibrationstart?'>Polynomial Calibration</a><p>");


  //add all your parameters here
  wm.addParameter(&custom_portalTimeOut);
  wm.addParameter(&custom_mqttHTML);
  wm.addParameter(&custom_mqtt_username);
  wm.addParameter(&custom_mqtt_password);
  wm.addParameter(&custom_mqtt_clientid);
  wm.addParameter(&custom_polynomialHTML);
  wm.addParameter(&custom_coefficientx3);
  wm.addParameter(&custom_coefficientx2);
  wm.addParameter(&custom_coefficientx1);
  wm.addParameter(&custom_constantterm);
  wm.addParameter(&custom_ispindleotherHTML);
  wm.addParameter(&custom_batconvfact);
  wm.addParameter(&custom_pubint);
  wm.addParameter(&custom_originalgravity);
  wm.addParameter(&custom_tiltOffset);    
  wm.addParameter(&custom_sensorpage_link);
  wm.addParameter(&custom_offsetcalibration_link);  
  wm.addParameter(&custom_polynomialcalibration_link); 
  // set static ip
  // IPAddress _ip,_gw,_sn;
  // _ip.fromString(static_ip);
  // _gw.fromString(static_gw);
  // _sn.fromString(static_sn);
  // wm.setSTAStaticIPConfig(_ip, _gw, _sn);

  //reset settings - wipe credentials for testing
  //wm.resetSettings();

  //automatically connect using saved credentials if they exist
  //If connection fails it starts an access point with the specified name
  //here  "AutoConnectAP" if empty will auto generate basedcon chipid, if password is blank it will be anonymous
  //and goes into a blocking loop awaiting configuration



  Serial.println();
  float tilt=calcTilt(); //
  Serial.println("You are in setup(), Tilt is: "+String(tilt)+ ", If tilt <12 degrees, The Wifi Manager configuration portal will run");
  if (tilt==NAN)
  {
    ESP.restart();
    delay(3000);
  }
  if (tilt<12)  //Condition to run The Wifi Manager portal.  
  {
    pinMode(LED_BUILTIN,OUTPUT);  //It seems like WiFimanager or WiFi frequently overwright this statement
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);    
    Serial.println("iSpindle will enter the WiFi configuration portal");
    Serial.println("Should run portal");
    wm.setConfigPortalTimeout(atof(portalTimeOut));
    if (!wm.startConfigPortal("iSpindle_Cayenne"))
    {
      Serial.println("WiFi Manager Portal: User failed to connect to the portal or dit not save. Time Out");
      digitalWrite(LED_BUILTIN, HIGH);
      delay(5000);
    }

  }
  else
  {
    Serial.println("Should not run portal");
    digitalWrite(LED_BUILTIN, HIGH);

    Serial.println("WiFi need to connect with WiFi.begin(). Cridentials stored by WiFi manager in flash memory");
    WiFi.begin();
    delay(1000);  //This may need to be increased with bad WiFi connection

  }

  // always start configportal for a little while

  // wm.startConfigPortal("AutoConnectAP","password");

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");

  Serial.println("local ip");
  Serial.println(WiFi.localIP());
  Serial.println(WiFi.gatewayIP());
  Serial.println(WiFi.subnetMask());

  Serial.println("WiFi Cridentials");
  Serial.println(WiFi.SSID());

  //read updated parameters
  strcpy(portalTimeOut, custom_portalTimeOut.getValue());
  strcpy(mqtt_username, custom_mqtt_username.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());
  strcpy(mqtt_clientid, custom_mqtt_clientid.getValue());
  strcpy(coefficientx3, custom_coefficientx3.getValue());
  strcpy(coefficientx2, custom_coefficientx2.getValue());
  strcpy(coefficientx1, custom_coefficientx1.getValue());
  strcpy(constantterm, custom_constantterm.getValue());
  strcpy(batconvfact, custom_batconvfact.getValue());
  strcpy(pubint, custom_pubint.getValue());
  strcpy(originalgravity, custom_originalgravity.getValue());
  strcpy(tiltOffset, custom_tiltOffset.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["portalTimeOut"]   = portalTimeOut;
    json["mqtt_username"] = mqtt_username;
    json["mqtt_password"]   = mqtt_password;
    json["mqtt_clientid"]   = mqtt_clientid;
    json["coefficientx3"] = coefficientx3;
    json["coefficientx2"]  = coefficientx2;
    json["coefficientx1"]  = coefficientx1;
    json["constantterm"]   = constantterm;
    json["batconvfact"]   = batconvfact;
    json["pubint"]   = pubint;
    json["originalgravity"]   = originalgravity;
    json["tiltOffset"]   = tiltOffset;


    // json["ip"]          = WiFi.localIP().toString();
    // json["gateway"]     = WiFi.gatewayIP().toString();
    // json["subnet"]      = WiFi.subnetMask().toString();

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.prettyPrintTo(Serial);
    json.prettyPrintTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
    shouldSaveConfig = false;
  }
  digitalWrite(LED_BUILTIN, HIGH); //Indicate WiFi Manager portal Stoped
  Serial.println("Try to connect to Cayenne with: " + String(mqtt_username) + ", " + String(mqtt_password) + ", " + String(mqtt_clientid));

  if (Ping.ping("mydevices.com")==true)  //WiFi.status() == WL_CONNECTED
  {
    Cayenne.begin(mqtt_username, mqtt_password, mqtt_clientid);
  }
  else
  { delay(4000); //if connection fail, wait 4 seconds, try again. If connection fails again, deep sleep
    Serial.println("Try to connect to Cayenne with: " + String(mqtt_username) + ", " + String(mqtt_password) + ", " + String(mqtt_clientid));
    if (Ping.ping("mydevices.com")==true)  //WiFi.status() == WL_CONNECTED
    {
      Cayenne.begin(mqtt_username, mqtt_password, mqtt_clientid);
    }
    else
    {
      Serial.println("Could not connect to Cayenne");
      startDeepSleep();
    }
  }


    sensors.begin();

}

void loop() {
  doWiFiManager();
  // put your main code here, to run repeatedly:
  if (portalRunning == false)
  {
    if (cayenneConnected==true)
      {Cayenne.loop();}
    else
    {
      startDeepSleep();
    }
  }
}

void doWiFiManager() {
  // is auto timeout portal running
  if (portalRunning) {

    wm.process(); // do processing

    // check for timeout
    if ((millis() - startTime) > (timeout * 1000)) {
      Serial.println("portaltimeout");
      portalRunning = false;
      if (startAP) {
        wm.stopConfigPortal();
      }
      else {
        wm.stopWebPortal();
      }
    }
  }
}

CAYENNE_OUT_DEFAULT()
{
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  delay(300);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(300);
  digitalWrite(LED_BUILTIN, LOW);
  delay(300);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(300);
  digitalWrite(LED_BUILTIN, LOW);
  delay(300);
  digitalWrite(LED_BUILTIN, HIGH);
  fpubint = atof(pubint);
  float tilt = calcTilt();

  float gravity = calcGrav();
  float temp = calcTemp();
  float abv = calcABV();


  float fbatconvfact = atof(batconvfact);
  float batvolt = analogRead(A0) / fbatconvfact;

  float foriginalgravity = atof(originalgravity);
  long signalstrength = WiFi.RSSI() + 90;

  Cayenne.virtualWrite(3, atof(coefficientx3) * 1000);
  Cayenne.virtualWrite(2, atof(coefficientx2) * 1000);
  Cayenne.virtualWrite(1, atof(coefficientx1) * 1000);
  Cayenne.virtualWrite(0, constantterm);
  Cayenne.celsiusWrite(4, temp);
  Cayenne.virtualWrite(5, tilt);
  Cayenne.virtualWrite(6, analogRead(A0) / fbatconvfact); //battery voltage
  Cayenne.virtualWrite(7, gravity);
  Cayenne.virtualWrite(8, fpubint);
  Cayenne.virtualWrite(9, foriginalgravity);
  Cayenne.virtualWrite(10, abv);
  Cayenne.virtualWrite(11, signalstrength);

  Serial.println("Data send to Cayenne..............................................");
  Serial.println("Tilt: " + String(tilt));
  Serial.println("Temp: " + String(temp));
  Serial.println("Gravity: " + String(gravity, 4));
  Serial.println("Original Gravity: " + String(foriginalgravity, 4));
  Serial.println("ABV: " + String(abv, 3));

  Serial.println("Battery voltage: " + String(batvolt, 3));
  Serial.println("Publication Interval in seconds to Cayenne: " + String(fpubint));
  Serial.println("Signal Strength: " + String(signalstrength));
  if (tilt > 12)
  {
    startDeepSleep();
  }
  else
  {
    Serial.println("The iSpindle is vertical, Did you use a magnet? Wifi Manager configuration portal will start");
    ESP.restart();
  }
}

CAYENNE_CONNECTED() {
  Serial.println("Cayenne connected. You are in CAYENNE_CONNECTED()");
  cayenneConnected=true;

}

CAYENNE_DISCONNECTED() {
  Serial.println("Disconnected from Cayenne. You are in Cayenne.CAYENNE_DISCONNECTED()...");
  cayenneConnected=false;
  startDeepSleep();  
//  while (true)
//  {
//    Serial.println("blocking");
//    digitalWrite(LED_BUILTIN, LOW);   
//    delay(1000);                      
//    digitalWrite(LED_BUILTIN, HIGH);  
//    delay(2000);
// }
}
