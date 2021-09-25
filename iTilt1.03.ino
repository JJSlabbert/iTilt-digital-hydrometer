/*Alternative firmware for iSpindle hydrometer/gravity meter. This firmware also suport an ESP32
   SPECS:
   Cloud service: Cayenne My Devices
   WifiManager on reset and vertical position 
   Deep Sleep: Working
   Starting Gravity and ABV
   Custom Pages: Censor readings, Offset Calibration, 3rd degree polynomial calibration
   Firmware Update (Only if Latest WiFimanager library from Github is used
   Start WiFiManager configuration portal during brew with a magnet. Use the magnet to draw the iSpindle vertical against the fermenter wal
   Remaining Battery Capacity estimates
   In Dev: Custom Pin Configuration
   LED INDICATOR
   1 second = iSpindle awake after deep sleep
   20 short flashes, one 1 second flas = iSpindle will go into ACCESS POINT mode.
   
   3 short flashes= Data was send to Cayenne cloud.
   TO DO: 
   ESP8266 PINS AND ESP32 PINS
   ESP32 Sensors powered via GPIO
   
   Dev by JJ Slabbert. https://www.instructables.com/member/JJ%20Slabbert/instructables/ and https://github.com/JJSlabbert
   I am not a profesional programmer or engineer. This is my hobby.
   English is my second language.
   I wrote this firmware, because
   1) The standard iSpindle firmware does not support Cayenne
   2) The firmware source code is to complicated for me to understand. The original Firmware is however great.
   3) The original firmware does not publish ABV
   4) One of my iSpindles MPU6050 Gyro have a Device ID problem and it could not show the Tilt in WiFiManager, althought it could publish the tilt to UBIDOTS
      https://forum.arduino.cc/t/mpu6050-connection-and-acceleration-offset-failure/651690 and https://github.com/universam1/iSpindel/issues/429
      
*/


//WiFiManager global declerations
#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#ifdef ESP32
  #include <SPIFFS.h>
#endif

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

//define your default values here, if there are different values in config.json, they are overwritten.
char portalTimeOut[5] = "9000";
char mqtt_username[37]; //Cayenne details
char mqtt_password[41];
char mqtt_clientid[37];
char coefficientx3[16] = "0.0000010000000"; //model to convert tilt to gravity
char coefficientx2[16] = "-0.000131373000";
char coefficientx1[16] = "0.0069679520000";
char constantterm[16] = "0.8923835598800";
#ifdef ESP8266
char batconvfact[8] = "193.00"; //converting reading to battery voltage 196.25
#endif
#ifdef ESP32
char batconvfact[8]="872.48";
#endif
char pubint[6] = "0"; // publication interval in seconds
char originalgravity[6] = "1.05";
char tiltOffset[5] = "0";
char dummy[3];


WiFiManager wm;

//flag for saving data (Custom params for WiFiManager
bool shouldSaveConfig = false;


//MQTT global declerations

#include <ArduinoMqttClient.h>
WiFiClient wifiClient;  //MQTT things
MqttClient mqttClient(wifiClient);
const char broker[] = "mqtt.mydevices.com";
int        port     = 1883;


//DS18B20 global decleration
#include <OneWire.h>  //Instal from Arduino IDE
#include <DallasTemperature.h> //Instal from Arduino IDE
#ifdef ESP8266
char onewire_pin[3]="12";
#endif
#ifdef ESP32
char onewire_pin[3]="33";
#endif

OneWire oneWire(atoi(onewire_pin));
DallasTemperature sensors(&oneWire);


//MPU6050 global decleration
#include <MPU6050.h> //Instal from Arduino IDE
#include <Wire.h>
#ifdef ESP8266
char i2c_sda_pin[3]="0";
char i2c_scl_pin[3]="2";

#endif
#ifdef ESP32
char i2c_sda_pin[3]="21";
char i2c_scl_pin[3]="22";
#endif
int i2c_address=0x68;
MPU6050 accelgyro(i2c_address);
int16_t ax, ay, az;

//ESP32 POWER PIN
#ifdef ESP32
char power_pin[3]= "32";
#endif

//GLOBAL HTML STRINGS
String htmlMenueText="<a href='/wifi?' class='button'>CONFIGURE WIFI, CAYENNE AND CALIBRATION</a>\
<br><a href='/readings?' class='button'>SENSOR READINGS</a>\
<br><a href='/offsetcalibration?' class='button'>OFFSET CALIBRATION</a>\    
<br><a href='/polynomialcalibrationstart?' class='button'>POLYNOMIAL CALIBRATION</a>\
<br><a href='/info?' class='button'>INFO</a>\ 
<br><a href='/exit?' class='button'>EXIT</a>\  
<br><a href='/update?' class='button'>FIRMWARE UPDATE</a>\
<br><a href='/pinconfinput?' class='button'>PIN CONFIGURATIONS</a>";  

String htmlStyleText= "<style>  body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088;  max-width:900px; align-content:center;} p {font-size: 30px;} h1 {text-align: center;}\
.button {  background-color: blue;  border: none;  color: white;  padding: 30px 15px;  text-align: center;  text-decoration: none;  display: inline-block;  font-size: 30px;  margin: 4px 2px;  cursor: pointer; width: 900px;} </style>";

//Other global declerations
#include <curveFitting.h>  //used for polynomial calibration

void startDeepSleep()
{
  float fpubint = atof(pubint);
  if (fpubint <60)
  {
    Serial.println("Data Publication Interval is less than 60 seconds, iTilt will not go to deep sleep but to setup()");
    delay(30000);
    setup();
  } 
  #ifdef ESP32
  delay(500);
  #endif
  digitalWrite(LED_BUILTIN,LOW);
  Serial.println("Entering deep sleep for " + String(fpubint) + " seconds");
  #ifdef ESP8266
  ESP.deepSleep(fpubint * 1000000);
  delay(1000);
  #endif
  #ifdef ESP32
  esp_sleep_enable_timer_wakeup(fpubint *  1000000);
  esp_deep_sleep_start();
  delay(1000);
  #endif
}

float calcOffset()
{ 
  float reading;
  float areading=0;
  float n=100;
  pinMode(atoi(i2c_sda_pin), OUTPUT);//This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  pinMode(atoi(i2c_scl_pin), OUTPUT); //This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  digitalWrite(atoi(i2c_sda_pin), LOW); //This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  digitalWrite(atoi(i2c_scl_pin), LOW);  //This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  delay(100);
  Wire.begin(atoi(i2c_sda_pin), atoi(i2c_scl_pin));
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
float calcBatCap(float volts)  //linear interpolation used, data from http://www.benzoenergy.com/blog/post/what-is-the-relationship-between-voltage-and-capacity-of-18650-li-ion-battery.html
{
  float capacity;
  if (volts<=3)
  {
    capacity=0;
    return capacity;
  }
  if (volts>3 and volts<=3.45)
  {
    capacity=0+((volts-3)/(3.45-3))*5;
    return capacity;    
  }
  if (volts>3.45 and volts<=3.68)
  {
    capacity=5+((volts-3.45)/(3.68-3.45))*5;
    return capacity;    
  }
  if (volts>3.68 and volts<=3.74)
  {
   capacity=10+((volts-3.68)/(3.74-3.68))*10;
   return capacity;   
  }
  if (volts>3.74 and volts<=3.77)
  {
    capacity=20+((volts-3.74)/(3.77-3.74))*10;
    return capacity;    
  }
  if (volts>3.77 and volts<=3.79)
  {
    capacity=30+((volts-3.77)/(3.79-3.77))*10;
    return capacity;    
  }
    
  if (volts>3.79 and volts<=3.82)
  {
    capacity=40+((volts-3.79)/(3.82-3.79))*10;
    return capacity;    
  }
  if (volts>3.82 and volts<=3.87)
  {
    capacity=50+((volts-3.82)/(3.87-3.82))*10;
    return capacity;    
  }                
  if (volts>3.87 and volts<=3.92)
  {
    capacity=60+((volts-3.87)/(3.92-3.87))*10;
    return capacity;    
  }       
  if (volts>3.92 and volts<=3.98)
  {
    capacity=70+((volts-3.92)/(3.98-3.92))*10;
    return capacity;    
  }       
  if (volts>3.98 and volts<=4.06)
  {
    capacity=80+((volts-3.98)/(4.06-3.98))*10;
    return capacity;    
  }      
  if (volts>4.06)
  {
    capacity=90+((volts-4.06)/(4.2-4.06))*10;
    if (capacity>100)
    {capacity=100;}
    return capacity;    
  }           
}

float calcBatVolt()
{
  float reading=0;
  float n=30;
  for (int i=0;i<n;i++)
  {
    reading+=(1/n)*analogRead(A0)/atof(batconvfact);
  }
  return reading;
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

  pinMode(atoi(i2c_sda_pin), OUTPUT);//This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  pinMode(atoi(i2c_scl_pin), OUTPUT); //This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  digitalWrite(atoi(i2c_sda_pin), LOW); //This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  digitalWrite(atoi(i2c_scl_pin), LOW);  //This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  delay(100);
  Wire.begin(atoi(i2c_sda_pin), atoi(i2c_scl_pin));
  Wire.beginTransmission(i2c_address);
  Wire.write(0x6B);  // PWR_MGMT_1 register
  Wire.write(0);     // set to zero (wakes up the MPU-6050)
  Wire.endTransmission(true);
 
  accelgyro.initialize();
  //Get Gyroscope readings untill it stoped reading nan (Not a Number)
  for (int i=0;i<1000;i++)
  {
    accelgyro.getAcceleration(&ax, &az, &ay);
    reading=acos(az / (sqrt(ax * ax + ay * ay + az * az))) * 180.0 / M_PI;
    Serial.println("You are in calcTilt(). reading="+String(reading));
    if (String(reading)!="nan")
    {
      break;
    }
    if (i==999)
    {
      Serial.println("You are in calcTilt(). The MPU6050 could not provide numerical readings for 1000 times.");
    }
  }
  
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

void bindServerCallback() {

  wm.server->on("/readings", handleReadings);
  wm.server->on("/offsetcalibration", handleOffsetCalibration);
  wm.server->on("/polynomialcalibrationstart", handlePolynomialCalibrationStart);
  wm.server->on("/polynomialcalibrationinput",handlePolynomialCalibrationInput);
  wm.server->on("/polynomialcalibrationresults",handlePolynomialCalibrationResults);
  wm.server->on("/pinconfinput",handlePinConfInput);
  wm.server->on("/updatepinconfresults",handlePinConfResults);
  wm.server->on("/",handleRoute); // you can override wm! main page
}
void handleRoute()
{
  Serial.println("[HTTP] handle Route");
  String htmlText = "<HTML><HEAD><TITLE>iTilt Main Page</TITLE></HEAD>";
  htmlText+= htmlStyleText;
  htmlText+= "<BODY><h1>CONFIGURATION PORTAL for iTilt</h1>";
  htmlText+=htmlMenueText;   
  htmlText += "</BODY></HTML>";  
  wm.server->send(200, "text/html", htmlText);
}


void handlePinConfResults()
{
  Serial.println("saving pinconfig");  
  DynamicJsonDocument json(1024);
  #ifdef ESP32  
  json["power_pin"]   = wm.server->arg(0);
  #endif
  json["onewire_pin"] =wm.server->arg(1);
  json["i2c_sda_pin"]   =wm.server->arg(2);
  json["i2c_scl_pin"]   = wm.server->arg(3);

  File pinConfigFile = SPIFFS.open("/pinconfig.json", "w");
  if (!pinConfigFile) {
    Serial.println("failed to open pinconfig file for writing");
  }

  serializeJson(json, Serial);
  serializeJson(json, pinConfigFile);

  pinConfigFile.close(); 
   
  String htmlText="<HTML><HEAD><TITLE>iTilt Custom Pin Configuration</TITLE></HEAD>";
  htmlText+= htmlStyleText;
  htmlText+= "<BODY><h1>CUSTOM PIN CONFIGURATION IS UPDATED AS FOLLOW</h1>";
  #ifdef ESP32  
  htmlText+="<p>"+wm.server->argName(0)+" "+String(wm.server->arg(0))+"<br>";
  #endif
  htmlText+=wm.server->argName(1)+" "+String(wm.server->arg(1))+"<br>";
  htmlText+=wm.server->argName(2)+" "+String(wm.server->arg(2))+"<br>";
  htmlText+=wm.server->argName(3)+" "+String(wm.server->arg(3))+"</p>";
  htmlText+="<p> The iTilt will restart in less than 30 seconds. Updates will take effect after restart.</p>";
 
  wm.server->send(200, "text/html", htmlText);
  delay(20000);
  ESP.restart();      
}

void handlePinConfInput()
{
  Serial.println("[HTTP] handle PinCpnfInput");
  String htmlText = "<HTML><HEAD><TITLE>iTilt Custom Pin Configuration</TITLE></HEAD>";
  htmlText+= htmlStyleText;
  htmlText+= "<BODY><h1>CUSTOM PIN CONFIGURATION for iTilt</h1>";
  htmlText+="<p>Thi iTilt uses default pin configurations for<br>";
  htmlText+="<b>ESP32:</b><br> Power=32, One Wire(DS18B20 Data)=33, GYRO MPU6050_SDA=21, GYRO MPU6050_SCL=22 <br>";
  htmlText+="<b>ESP8266 (iSpindel):</b><br> One Wire(DS18B20 TEMP Data)=12, MPU6050_SDA=0, MPU6050_SCL=2<br>";
  htmlText+="You may re congigure these pins for your custom design";
  htmlText+="<p>Current Pin Configuration<br>";
  #ifdef ESP32
  htmlText+="Power Pin: "+String(power_pin)+"<br>";
  #endif
  htmlText+="One Wire Pin: "+String(onewire_pin)+"<br>";
  htmlText+="i2c SDA Pin: "+String(i2c_sda_pin)+"<br>"; 
  htmlText+="i2C SCL Pin: "+String(i2c_scl_pin)+"</p>"; 
  htmlText+="<p><form action='/updatepinconfresults?' method ='POST'>";
  #ifdef ESP32
  htmlText+="<font size='5'>POWER PIN (ESP32 ONLY)</font><br><input type='number' name='Power Pin' size='5' min='0' max='40' step='1' value="+String(power_pin)+" style='height:80px;font-size:30pt;'><br>";
  #endif
  htmlText+="<font size='5'>ONE WIRE BUS</font><br><input type='number' name='One Wire Pin' size='5' min='0' max='40' step='1' value="+String(onewire_pin)+" style='height:80px;font-size:30pt;'><br>"; 
  htmlText+="<font size='5'>i2c_SDA</font><br><input type='number' name='i2c SDA Pin' size='5' min='0' max='40' step='1' value="+String(i2c_sda_pin)+" style='height:80px;font-size:30pt;'><br>"; 
  htmlText+="<font size='5'>i2c_SCL</font><br><input type='number' name='i2c SCL' size='5' min='0' max='40' step='1' value="+String(i2c_scl_pin)+" style='height:80px;font-size:30pt;'><br>";
  htmlText+="<br><input type='submit' value='UPDATE PINS AND RESTART' style='height:80px;font-size:30pt;'>";
  htmlText+="</form></p>";         
  htmlText+=htmlMenueText;
  wm.server->send(200, "text/html", htmlText);
}
  
void handleOffsetCalibration()
{ 
  float offset=calcOffset(); 
  Serial.println("[HTTP] handle Offset Calibration");
  Serial.println("OFFSET Calculated, Send to /offsetcalibration?: "+String(offset));
  String htmlText = "<HTML><HEAD><TITLE>iTilt Offset Calibration</TITLE></HEAD>";
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
  int numargs=wm.server->args();
  Serial.println("Sample Size in handlePolynomialCalibrationResults() : "+String(n));

  String htmlText = "<HTML><HEAD><TITLE>iTilt POLYNOMIAL RESULTS</TITLE></HEAD>";
  htmlText+= htmlStyleText;
  htmlText += "<BODY><h1>POLYNOMIAL CALIBRATION WIZARD: RESULTS</h1>";

//  for (int i=0; i<wm.server->args()-1;i++)
//      {
//        text+= "Argument "+String(i)+", Argument Name: "+wm.server->argName(i)+", Argument Value: "+String(wm.server->arg(i));
//        text+="<br>";        
//      }

  for (int i=0; i<numargs;i++)
  {
    Serial.println("Argument number: "+String(i)+", Argument Name: "+wm.server->argName(i)+", Argument Value: "+String(wm.server->arg(i)));
  }

  
  double sampledtilt[n];
  double sampledgrav[n];
  int recordnumber=0;
  htmlText+="<table border='1'><align='right'><tr><td>TILT VALUES</td><td>GRAVITY VALUES</td></tr>";
  for (int i=0; i<numargs; i=i+2)
  {
    sampledtilt[recordnumber]=atof(wm.server->arg(i).c_str());
    sampledgrav[recordnumber]=atof(wm.server->arg(i+1).c_str());
    htmlText+="<tr><td>"+String(sampledtilt[recordnumber])+"</td><td>"+String(sampledgrav[recordnumber],3)+"</td></tr>";
    recordnumber++;
  }
//  {
//    sampledtilt[i]=atof(wm.server->arg(i).c_str());
//    Serial.println("sampled tilt"+String(i)+": "+String(sampledtilt[i]));
//    sampledgrav[i]=atof(wm.server->arg(i+n).c_str());
//    Serial.println("sampled gravity"+String(i)+": "+String(sampledgrav[i],3)); 
//    htmlText+="<tr><td>"+String(sampledtilt[i])+"</td><td>"+String(sampledgrav[i],3)+"</td></tr>";
//    
//    //htmlText+= "Sampled_Tilt"+String(sampledtilt[i]) +", Sampled_Gravity"+String(sampledgrav[i],3)+"<br>";  
//  }
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
  String htmlText="<HTML><HEAD><TITLE>iTilt Polynomial Calibration</TITLE></HEAD><style>  body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; } </style>";
  htmlText+= htmlStyleText;
  htmlText += "<BODY><h1>POLYNOMIAL CALIBRATION WIZARD: SAMPLED DATA</h1>";
  htmlText+="<p>This wizard should assist you in calibrating the iTilt. If you are here, you may already have a calibration data sample set (ordered pairs of Tilt(Measured in <a href='/readings?'>SENSOR READINGS</a>) ";
  htmlText+="and Gravity (Measured with a Hydrometer). If not, you can do it now. Make sure your publication interval is set to 0, and portal time out is 9999. </p>";
  htmlText+="<p>We suggest you use a 3L measuring jar, boil 0.55kg sugar in 1.5L clean water. Let the mix cool down to about 20 degrees Celsius. Add your mix to the jar. ";
  htmlText+="Full the jar with extra water until it reaches 2.5L. Stir the content properly (before each measurement). Measure your Tilt, Measure you Gravity (If you have a hydro meter). If not, use the theoretical values.";
  htmlText+="<p>The instructions in the 3rd column is a guide only, you may ignore them</p>";
  htmlText+="<p>ALL RECORDS MUST BE COMLETED. The polynomial will be WRONG otherwise</p>";
  
  htmlText+= "<p><table style='height:80px;font-size:20pt;' border='1'><tr><td><br><form action='/polynomialcalibrationresults?' method ='POST'><b> TILT:</b></td><td><b>GRAVITY:</b></td><td><b>WIZARD INSTRUCTIONS AND THEORETICAL GRAVITY</b></td></tr>";
  int n=atoi(wm.server->arg(0).c_str());
  Serial.println("Sample Size in handlePolynomialCalibrationInput() : "+String(n));
  for (int i=0; i<n; i++)
  {
    htmlText+= "<tr><td><input type='number' name=+""tilt"+String(i) +" size='5' min='20.00' max='80.00' step='0.01' style='height:80px;font-size:30pt;'></td>";
    htmlText+= "<td><input type='number' name=+""gravity"+String(i) +" size='5' min='1' max='1.12' step='0.001' style='height:80px;font-size:30pt;'></td>";
    if (i==0)
    {
     htmlText+="<td>";
     htmlText+="Your sugar content in the jar of 2.5L is 500g. The Theoretical Gravity is 1.084 SG";
     htmlText+="</td></tr>";     
    }
    else if (i==1)
    {
     htmlText+="<td>";
     htmlText+="Before any measurement, remove 600 ml of mix in jar and replace it with 600 ml clean water. ";
     htmlText+="Your sugar content in the jar of 2.5L is 410 g. The Theoretical Gravity is 1.064 SG";           
     htmlText+="</td></tr>";          
    }
    else if (i==2)
    {
     htmlText+="<td>";
     htmlText+="Before any measurement, remove 700 ml of mix in jar and replace it with 700 ml clean water. ";
     htmlText+="Your sugar content in the jar of 2.5L is 301 g. The Theoretical Gravity is 1.046 SG";       
     htmlText+="</td></tr>";          
    }
    else if (i==3)
    {
     htmlText+="<td>";
     htmlText+="Before any measurement, remove 800 ml of mix in jar and replace it with 800 ml clean water. ";
     htmlText+="Your sugar content in the jar of 2.5L is 205 g. The Theoretical Gravity is 1.032 SG";              
     htmlText+="</td></tr>";        
    } 
    else if (i==4)
    {
     htmlText+="<td>";
     htmlText+="Before any measurement, remove 900 ml of mix in jar and replace it with 900 ml clean water. ";
     htmlText+="Your sugar content in the jar of 2.5L is 131 g. The Theoretical Gravity is 1.020 SG";              
     htmlText+="</td></tr>";        
    } 
    else if (i==5)
    {
     htmlText+="<td>";
     htmlText+="Before any measurement, remove 1100 ml of mix in jar and replace it with 1100 ml clean water. ";
     htmlText+="Your sugar content in the jar of 2.5L is 73 g. The Theoretical Gravity is 1.011 SG";              
     htmlText+="</td></tr>";          
    } 
    else if (i==6)
    {
     htmlText+="<td>"; 
     htmlText+="Before any measurement, remove 1300 ml of mix in jar and replace it with 1300 ml clean water. ";
     htmlText+="Your sugar content in the jar of 2.5L is 35 g. The Theoretical Gravity is 1.005";             
     htmlText+="</td></tr>";           
    }
    else
     htmlText+="<td></td></tr>";     
  }
  htmlText += "</table><br><input type='submit' value='Submit' style='height:80px;font-size:30pt;'>  </form></p><br>";
  htmlText += "<br></BODY></HTML>";  
  wm.server->send(200, "text/html", htmlText);
}

void handlePolynomialCalibrationStart()
{
  String htmlText = "<HTML><HEAD><TITLE>iTilt Calibration Page</TITLE></HEAD>";
  htmlText+= htmlStyleText;
  htmlText+= "<BODY><h1>POLYNOMIAL CALIBRATION WIZARD</h1>";
  htmlText+="<p> This links will not work if you are connected to the iTilt Access point.</p>";
  htmlText+="<p>Sugar Wash calculators will indicate how much sugar you need to add into a specific amount of water to reach certain gravity</p>";
  htmlText+="<p><a href='https://chasethecraft.com/calculators'>Sugar wash Calculator at https://chasethecraft.com/calculators</a></p>";
  htmlText+="<p><a href='https://www.hillbillystills.com/distilling-calculator'>Sugar wash Calculator at https://www.hillbillystills.com/distilling-calculator</a></p>";  
  htmlText+="<p><a href='https://www.ispindel.de/docs/Calibration_en.html#easy-method-(I)'>Methods of Calibration on https://www.ispindel.de/docs/Calibration_en.html#easy-method-(I)</a></p>";
  htmlText+="<p><a href='https://www.ispindel.de/tools/calibration/calibration.htm'>Calibration Calculater on https://www.ispindel.de/tools/calibration/calibration.htm</a></p>"; 
  htmlText+="<p> <b>POLYNOMIAL CALIBRATION WIZARD</b></p>";
  htmlText+="It is recomended to use a sample size of 7 and the and water volumes and sugar weights provided in the next step.";  
  htmlText+="<br><br><form action='/polynomialcalibrationinput?' method ='POST'> SAMPLE SIZE (6-30): <input type='number' name='sample_size' size='5' min='6' max='30' step='1' style='height:80px;font-size:30pt;'><input type='submit' value='Submit' style='height:80px;font-size:30pt;'>  </form><br>";
  //htmlText += "<p><a href='/wifi?'>--Configure Wifi--</a><a href='/readings?'>--Sensor Readings--</a> <a href='/offsetcalibration?'>--Offset Calibration--</a><a href='/polynomialcalibrationstart?'>--Polynomial Calibration--</a><a href='/'>--Main Page--</a></p>";
  htmlText+=htmlMenueText;   
  
  htmlText += "</BODY></HTML>";  
  wm.server->send(200, "text/html", htmlText);    
}

void handleReadings() {
  float batvolt=calcBatVolt();
  Serial.println("[HTTP] handle Readings");
  String htmlText = "<HTML><HEAD><meta http-equiv='refresh' content='1'><TITLE>iTilt Sensor Readings</TITLE></HEAD>";
  htmlText+= htmlStyleText;
  htmlText+= "<BODY><h1>iTilt Sensor Readings</h1>";
  htmlText+= "<p>Sensor Readings will update every 2 seconds.</p>";
  htmlText+= "<p>Battery Voltage: " + String(batvolt,2) + "</p>";
  htmlText+= "<p>Battery Remaining Capacity: " + String(calcBatCap(batvolt),0) + " percent</p>";  
  htmlText+= "<p>Tilt: " + String(calcTilt()) + " Degrees. This value should be about 89 degrees if the iTilt is on a horizontal surface.</p>";
  htmlText+= "<p>Gravity: " + String(calcGrav(), 5) + " SG</p>";
  htmlText+= "<p>Temperature: " + String(calcTemp()) + " Degrees Celcius</p>";
  htmlText+= "<p>Alcohol by Volume (ABV): " + String(calcABV()) + " %</p>";
  //htmlText += "<p><a href='/wifi?'>--Configure Wifi--</a><a href='/readings?'>--Sensor Readings--</a> <a href='/offsetcalibration?'>--Offset Calibration--</a><a href='/polynomialcalibrationstart?'>--Polynomial Calibration--</a><a href='/'>--Main Page--</a></p>";
  htmlText+=htmlMenueText; 
  htmlText += "</BODY></HTML>";
  wm.server->send(200, "text/html", htmlText);
}

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}


void setup() 
{
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN,HIGH);
  //clean FS, for testing
  //SPIFFS.format();
  //wm.resetSettings();
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

        DynamicJsonDocument json(1024*2);
        auto deserializeError = deserializeJson(json, buf.get());
        serializeJson(json, Serial);
        if ( ! deserializeError ) {

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
          strcpy(dummy,json["dummy"]);
        } else {
          Serial.println("failed to load json config");
        }
        configFile.close();
      }
    }

      if (SPIFFS.exists("/pinconfig.json")) {
      //file exists, reading and loading
      Serial.println("reading Pin config file");
      File pinConfigFile = SPIFFS.open("/pinconfig.json", "r");
      if (pinConfigFile) {
        Serial.println("opened Pin config file");
        size_t size = pinConfigFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        pinConfigFile.readBytes(buf.get(), size);

        DynamicJsonDocument json(1024*2);
        auto deserializeError = deserializeJson(json, buf.get());
        serializeJson(json, Serial);
        if ( ! deserializeError ) {

          Serial.println("\nparsed json");
          #ifdef ESP32
          strcpy(power_pin, json["power_pin"]);
          #endif
          strcpy(i2c_sda_pin, json["i2c_sda_pin"]);
          strcpy(i2c_scl_pin, json["i2c_scl_pin"]);
          strcpy(onewire_pin, json["onewire_pin"]);

        } else {
          Serial.println("failed to load json pinconfig");
        }
        pinConfigFile.close();
      }
    }

    
  } else {
    Serial.println("failed to mount FS");  //This hapen with new ESP32 KOALA WROVER-Format Spifs?
  }


  //end read

  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  #ifdef ESP32  //power the MPU6050 and DS18B20 from GPIO PINS OR SWITCH THEM WITH TRANSISTORS
    Serial.println("Power Pin: "+String(atoi(power_pin)) +" must go high");
    pinMode(atoi(power_pin),OUTPUT);
    digitalWrite(atoi(power_pin),HIGH);
    delay(200);
  #endif
  sensors.begin();
  float tilt=calcTilt();
  float grav=calcGrav();
  float abv=calcABV();
  float temp=calcTemp();  
  #ifdef ESP32
    digitalWrite(atoi(power_pin),LOW); 
  #endif    
  float fcoefficientx3;
  float fcoefficientx2;
  float fcoefficientx1;
  float fconstantterm;
  float foriginalgravity;
  float fpubint;
  float batvolt=calcBatVolt();
  float batcap=calcBatCap(batvolt);
  String polynome;
  String topic;
  String topic1;
  String topic2;
  String topic3;
  String topic4;
  String topic5;
  String topic6;   
  String topic7;
  String topic8;
  String topic9;
  String topic10;
  String topic11;
  
  Serial.println("You are in setup(), Tilt is: "+String(tilt)+ ", If tilt <12 degrees or nan, The Wifi Manager configuration portal will run");
  if ((tilt<95 and tilt>85) or tilt<12 or String(tilt)=="nan")  //Condition to run The Wifi Manager portal.  
  {
    pinMode(LED_BUILTIN,OUTPUT);  //It seems like WiFimanager or WiFi frequently overwright this statement
    for (int i=0;i<10;i++)
      {
      digitalWrite(LED_BUILTIN, LOW);
      delay(100);
      digitalWrite(LED_BUILTIN, HIGH);
      delay(100);
      }
 
    digitalWrite(LED_BUILTIN, HIGH);    
    Serial.println("iTilt will enter the WiFi configuration portal");
    Serial.println("Should run portal");

    WiFiManagerParameter custom_portalTimeOut("portalTimeOut", "WiFi Manager Configuration Portal Time Out", portalTimeOut, 4);
    WiFiManagerParameter custom_mqttHTML("<p><font size='4'><b>Provide the Parameters of Cayenne</b></font></p>");
    WiFiManagerParameter custom_mqtt_username("usernname", "MQTT USERNAME", mqtt_username, 36);
    WiFiManagerParameter custom_mqtt_password("pasword", "MQTT PASSWORD", mqtt_password, 40);
    WiFiManagerParameter custom_mqtt_clientid("clientid", "MQTT CLIENT ID", mqtt_clientid, 36);
    WiFiManagerParameter custom_polynomialHTML("<p><font size='4'><b>Provide the Parameters of you Polynomial</b></font></p>");                                                      //Custom HTML
    WiFiManagerParameter custom_coefficientx3("coefficientx3", "Coefficient of Tilt^3 (Regression Model Polynomial)", coefficientx3, 15);
    WiFiManagerParameter custom_coefficientx2("coefficientx2", "Coefficient of Tilt^2 (Regression Model Polynomial)", coefficientx2, 15);
    WiFiManagerParameter custom_coefficientx1("coefficientx1", "Coefficient of Tilt^1 (Regression Model Polynomial)", coefficientx1, 15);
    WiFiManagerParameter custom_constantterm("constantterm", "Constant Term (Regression Model Polynomial)", constantterm, 15);
    WiFiManagerParameter custom_iTiltotherHTML("<p><font size='4'><b>Other Parameters for the iTilt</b></font></p>");                                                           //Custom HTML
    WiFiManagerParameter custom_batconvfact("batconvfact", "Battery Conversion Factor", batconvfact, 7);
    WiFiManagerParameter custom_pubint("pubint", "Data Publication Interval", pubint, 5);
    WiFiManagerParameter custom_originalgravity("originalgravity", "Original Gravity", originalgravity, 5);
    WiFiManagerParameter custom_tiltOffset("tiltOffset", "Calibrated Tilt Offset", tiltOffset, 4);
    WiFiManagerParameter custom_sensorpage_link("<p><a href='/readings?'>Live sonsor readings</a><p>");
    WiFiManagerParameter custom_offsetcalibration_link("<p><a href='/offsetcalibration?'>Tilt Offset Calibration</a><p>");
    WiFiManagerParameter custom_polynomialcalibration_link("<p><a href='/polynomialcalibrationstart?'>Polynomial Calibration</a><p>");
  
    //wm.resetSettings();
    //Setting Callbacks
    wm.setSaveConfigCallback(saveConfigCallback);
    wm.setWebServerCallback(bindServerCallback);  

  
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
    wm.addParameter(&custom_iTiltotherHTML);
    wm.addParameter(&custom_batconvfact);
    wm.addParameter(&custom_pubint);
    wm.addParameter(&custom_originalgravity);
    wm.addParameter(&custom_tiltOffset);    
    wm.addParameter(&custom_sensorpage_link);
    wm.addParameter(&custom_offsetcalibration_link);  
    wm.addParameter(&custom_polynomialcalibration_link);
    wm.setConfigPortalTimeout(atoi(portalTimeOut));
    #ifdef ESP32
      pinMode(atoi(power_pin),OUTPUT);
      digitalWrite(atoi(power_pin),HIGH);
     
    #endif        
    
    if (!wm.startConfigPortal("iTilt"))
    {
      Serial.println("WiFi Manager Portal: User failed to connect to the portal or dit not save. Time Out");
      ESP.restart();
      digitalWrite(LED_BUILTIN, HIGH);
      delay(5000);
    }

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
    strcpy(dummy,"xx");
  
    //save the custom parameters to FS
    if (shouldSaveConfig) {
      Serial.println("shouldSaveConfig is: "+String(shouldSaveConfig));
      Serial.println("saving config");  
      DynamicJsonDocument json(1024);  
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
      json["dummy"]   = dummy;  
  
      File configFile = SPIFFS.open("/config.json", "w");
      if (!configFile) {
        Serial.println("failed to open config file for writing");
      }
  
      serializeJson(json, Serial);
      serializeJson(json, configFile);
  
      configFile.close();
      //end save
    }    

    mqttClient.setUsernamePassword(String(mqtt_username), String(mqtt_password));
    mqttClient.setId(String(mqtt_clientid));
    foriginalgravity=atof(originalgravity);
    fpubint=atof(pubint);    
    fcoefficientx3=atof(coefficientx3);
    fcoefficientx2=atof(coefficientx2);
    fcoefficientx1=atof(coefficientx1);
    fconstantterm=atof(constantterm);
    tilt=calcTilt();
    grav=calcGrav();
    abv=calcABV();
    temp=calcTemp();
    batvolt=calcBatVolt();
    batcap=calcBatCap(batvolt); 
    #ifdef ESP32
      pinMode(atoi(power_pin),OUTPUT);
      digitalWrite(atoi(power_pin),LOW);
      
    #endif  
    polynome=String(fcoefficientx3,8)+", "+String(fcoefficientx2,6)   +", "+String(fcoefficientx1,5)+", "+String(fconstantterm,5);
    topic="v1/"+String(mqtt_username)+"/things/"+String(mqtt_clientid)+"/data/";
    topic1=topic+"1";
    topic2=topic+"2";
    topic3=topic+"3";
    topic4=topic+"4";
    topic5=topic+"5";
    topic6=topic+"6";   
    topic7=topic+"7";
    topic8=topic+"8";
    topic9=topic+"9";
    topic10=topic+"10";
    topic11=topic+"11";    
  }
  else  //This is when tilt>12, nan or between 85 and 95 degrees and setup should not run
  {
    Serial.println("Should not run portal");
    digitalWrite(LED_BUILTIN, HIGH);
    mqttClient.setUsernamePassword(String(mqtt_username), String(mqtt_password));
    mqttClient.setId(String(mqtt_clientid));
    foriginalgravity=atof(originalgravity);
    fpubint=atof(pubint); 
    fcoefficientx3=atof(coefficientx3);
    fcoefficientx2=atof(coefficientx2);
    fcoefficientx1=atof(coefficientx1);
    fconstantterm=atof(constantterm);      
    polynome=String(fcoefficientx3,8)+", "+String(fcoefficientx2,6)   +", "+String(fcoefficientx1,5)+", "+String(fconstantterm,5);
    topic="v1/"+String(mqtt_username)+"/things/"+String(mqtt_clientid)+"/data/";
    topic1=topic+"1";
    topic2=topic+"2";
    topic3=topic+"3";
    topic4=topic+"4";
    topic5=topic+"5";
    topic6=topic+"6";   
    topic7=topic+"7";
    topic8=topic+"8";
    topic9=topic+"9";
    topic10=topic+"10";
    topic11=topic+"11";   


  }
  if (tilt>85)
  {
    Serial.println("Tilt is larger than 85 degrees. It seems like it is not in your brew yet. ESP will enter deep sleep");
    startDeepSleep();
  }
    Serial.println("WiFi need to connect with WiFi.begin(). Cridentials stored by WiFi manager in flash memory");
    WiFi.begin();
  
  for (int i=0;i<45;i++)  //Check if WiFi is connected
  {
    if (WiFi.status()==WL_CONNECTED)
    {
      break;
    }
    if (i==44)
    {
      Serial.println("The iTilt Could not connect to your WiFi Access Point. Deep Sleep will start...");
      startDeepSleep();
    }
    delay(200);
  }
  float signalstrength=(WiFi.RSSI()+90)*1.80;
  if (signalstrength>100)
  {signalstrength=100;}
  Serial.println("The iTilt connected to your WiFi access point");
  Serial.println("local ip");
  Serial.println(WiFi.localIP());

  Serial.println("Try to connect to Cayenne with: " + String(mqtt_username) + ", " + String(mqtt_password) + ", " + String(mqtt_clientid));
  if (mqttClient.connect(broker, port)==false) //is the same as   if (!mqttClient.connect(broker, port))    OR     if (mqttClient.connect(broker, port)==0)
  {    
    Serial.print("MQTT connection failed! Error code = ");
    Serial.println(mqttClient.connectError());
    delay(60000);
    startDeepSleep();
  }

  Serial.println("You're connected to the MQTT broker of Cayenne!");
  Serial.println("Topic 1: "+topic1);
  Serial.println("Battery Capacity: "+String(batcap,0));
  Serial.println("Topic 2: "+topic2);
  Serial.println("Polynome: "+polynome);
  Serial.println("Topic 3: "+topic3);
  Serial.println("Firmware version");  
  Serial.println("Topic 4: "+topic4);
  Serial.println("Temp: "+String(temp,4));  
  Serial.println("Topic 5: "+topic5);
  Serial.println("Tilt: "+String(tilt,4));  
  Serial.println("Topic 6: "+topic6);
  Serial.println("Battery Voltage: "+String(batvolt,4));  
  Serial.println("Topic 7: "+topic7);
  Serial.println("Gravity: "+String(grav,4));  
  Serial.println("Topic 8: "+topic8);
  Serial.println("Publication Interval: "+String(fpubint,4));  
  Serial.println("Topic 9: "+topic9);
  Serial.println("Original Gravity: "+String(foriginalgravity,4));  
  Serial.println("Topic 10: "+topic10);
  Serial.println("ABV: "+String(abv,4)); 
  Serial.println("Topic 11: "+topic11);
  Serial.println("Signal Strength: "+String(signalstrength,4)); 
  mqttClient.beginMessage(topic1);
  mqttClient.print(batcap); 
  mqttClient.endMessage();        
  mqttClient.beginMessage(topic2);
  mqttClient.print(polynome); 
  mqttClient.endMessage();
  mqttClient.beginMessage(topic3);
  mqttClient.print(1.03); 
  mqttClient.endMessage();
  mqttClient.beginMessage(topic4);
  mqttClient.print("temp,c="+String(temp,4)); 
  mqttClient.endMessage();
  mqttClient.beginMessage(topic5);
  mqttClient.print(tilt); 
  mqttClient.endMessage();
  mqttClient.beginMessage(topic6);
  mqttClient.print(batvolt); 
  mqttClient.endMessage();
  mqttClient.beginMessage(topic7);
  mqttClient.print(grav); 
  mqttClient.endMessage();
  mqttClient.beginMessage(topic8);
  mqttClient.print(fpubint); 
  mqttClient.endMessage();
  mqttClient.beginMessage(topic9);
  mqttClient.print(foriginalgravity); 
  mqttClient.endMessage();
  mqttClient.beginMessage(topic10);
  mqttClient.print(abv); 
  mqttClient.endMessage();
  mqttClient.beginMessage(topic11);
  mqttClient.print(signalstrength); 
  mqttClient.endMessage();
  startDeepSleep();      
}

void loop()
{
  //void loop() should never execute

}
