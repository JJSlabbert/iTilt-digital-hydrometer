![image](https://user-images.githubusercontent.com/38969599/133005694-9bf50485-bf34-47e5-b2d7-7a83a949222b.png)

![indevelopment](https://user-images.githubusercontent.com/38969599/133666654-350132da-1551-40de-bef8-71a61f215067.jpg)


# INTRODUCTION
An alternative firmware (ESP8266 and ESP32 compatible) for the iSpindel WiFi hydrometer developed with the Arduino IDE and using Cayenne cloud service. This firmware was tested on iSpindels and my iTilt version using an ESP32 Wemos Lolin.
The iTilt is an open source device used to measure the gravity (Density) of beer or cider. It can also be used to calculate the ABV (Alcohol by Volume) of you home brew beer.
The iTilt consist of an ESP8266 microcontroller (or ESP32), MPU6050 Gyroscope, battery and battery charger inside a PET Test tube.  The iTilt is inserted into your brew before you add yeast. Initially your brew will have a high gravity (density) because of all the sugar in it. The iTilt may tilt at about 60° (from a vertical line) in your brew. When your brew starts to ferment and sugar is turned into alcohol, the tilt angle will reduce and the iTilt will become more vertical. The gyroscope will continuously monitor this tilt, transform it into gravity (with a statistical model) and ABV and publish the results to the cloud.

More info on the iSpindel is available at https://www.ispindel.de/docs/README_en.html and https://github.com/universam1/iSpindel

The standard original iSpindel firmware code was developed in PlatformIO and publishes data to Ubidots, Bierbot Bricks, Blynk etc and few other services. This original source code is complicated (for me) and I wanted something more simplified publishing to Cayenne https://cayenne.mydevices.com/ since I use Cayenne for my other projects.

# Diffirences between this firmware/Hardware and the standard original iSpindel

<table border="1"><tr><td> <b>iTilt</b></td><td> <b>iSpindel</b></td></tr>
  <tr><td>1 Publish to Cayenne.</td><td>1 Publish to ubidots and other services.	</td></tr>
  <tr><td>2 Developed in Arduino IDE.</td><td>2 Developed in PlatformIO.</td></tr>
  <tr><td>3 Simple for me.</td><td>3 Complicated for me.</td></tr>
  <tr><td>4 Some Functionality without the cloud.(In development, No Logging functionality</td><td>4 Need a cloud or an external Server like Raspberry PI.</td></tr>
  <tr><td>5 WiFimanager configuration portal can be loaded while brewing.</td><td>5 WiFimanager configuration portal can’t be loaded while brewing.</td></tr>
  <tr><td>6 Polynomial calibration can be done in the WiFiManager Configuration portal with an easy to use wizard. If you use the theoretical gravities suggested in the wizard, you do not even need an extra hydrometer to calibrate.</td><td>6 Calibration done in Excel.</td></tr>
  <tr><td>7 Alcohol By Volume is calculated and published to Cayenne, together with Tilt, Current Gravity,Temperature, Original Gravity, the Coefficients of the Polynomial, battery voltage and WiFi Signal Strength.</td><td>7 Data published is Tilt, Gravity, Temperature, Battery Voltage and WiFi Signal Strength.</td></tr>
  <tr><td>8 Can run on an ESP32 (Was tested on Wemos lolin 32 and TTGO KOALA) and ESP8266 (Wiring must be according to iSpindel standards).</td><td>8 Only run on ESP8266.</td></tr>
  <tr><td>9 It should consume less power.This need testing. MPU6050 and DS18B20 is powered by GPIO and switch of during deep sleep.</td><td>9 May consume more power.</td></tr>
  <tr><td>10 One Micro USB port for charging and programming.</td><td>10 One Micro USB port for programming and one USB C port for charging.</td></tr>
  <tr><td>11 Less complicated cuircut. The Wemos Lolin and TTGO Koala has a build in battery chargers/power supplies.</td><td>11 ESP8266 D1 Mini does not have a battery charger/power supply. Extra battery unit required</td></tr> 
<table>

You will need an iSpindel / iTilt hardware and Cayenne credentials.
You need to be skilled in using the Arduino IDE or Loading bin file firmware on an ESP8266. I will not be Liable if you mess up your EPROM or damage anything. 

# BUILD YOUR HARDWARE
  
 Note, My 2 ESP32 Wemos Lolins could not power the MPU6050 and DS18B20 via GPIO when on battery. You need to wire them with the 3.3 volt pin. You may switch them on and off with a transistor.
  
 On the ESP32 TTGO KOALA GPIO 16 and 17 was both able to power both the MPU6050 and DS18B20 simultaneously.

![fritzing](https://user-images.githubusercontent.com/38969599/134550321-57f0f657-c37c-428a-87b3-d3e5ef49a665.jpg)
  
 
To Do: Eagle File PCB

# LIBRARY REQUIREMENTS
-WiFimanager (Install the latest from Github https://github.com/tzapu/WiFiManager). The arduino library 2.0.3-alpha is out dated and does not support firmware updates.

-ArduinoJson version 6 (Install from Arduino IDE)

-ArduinoMqttClient (Install from Arduino IDE) for iTilt v1.01, CayenneMQTT (Install from Arduino IDE) for v1.00

-OneWire (Install from Arduino IDE)

-DallasTemperature (Install from Arduino IDE)

-MPU6050 (Install from Arduino IDE)

# STEPS
-Load the Firmware from source or binary on your iSpindel. If you had the iSpindel firmware (or other firmware using EEPROM) installed, you may need format your SPIFFS. Read the disclaimer.

-The LED_BUILTIN (Blue LED in my case) should flash

-Short the two reset pins (press reset) and tilt the iTilt vertical (Cup, USB port must be on top)

-Place your iTilt horizontal on a levelled surface

-You will now see that the iTilt created an Access point.

-Connect to the AP. Go to http://192.168.4.1 if it is not automaticly loaded.

-Provide all the required details in Configure Wifi. You will need your Cayenne cridentials and a calibrated polynomial if already done (The statistical model to transform tilt into gravity.  See https://www.ispindel.de/docs/README_en.html). Always re select your Internet WiFi AP and retype your password in the configuration page, otherwise no parameters will be saved.

-If you still need to calibrate your polynomial, make the portal time out 9000 and publication interval 0. Publication interval =0 means deep sleep is avoided and data is published to Cayenne +- every 20 seconds.

-Do not enter your Original Gravity yet (Except if you measured it with another hydrometer or got it from your recipe).

-Save the provided details.

-Reset your WiFimanager configuration portal by shorting the reset pins and tilt the decice vertical.

-Log into your iTilt AP again.

-Make sure all details saved is correct in WiFi Configure WiFi.

-You can now do an offset calibration. Remember to place your iSpindel vertical. You should manualy insert the offset (with its sign) in the Configure WiFi page.

-Save the offset (remember to re select your internet access point and password)

-Reconect to the Configuration Portal. Do a polynomial calibration by following the wizard. It is recomended to do easy calibration with 7 ordered pairs (sample size=7) of (tilt, gravity). You will need a jar (+/- 3L), clean water and 0.55 kg sugar. Manualy enter the coefficients in the Configure WiFi page. SAVE. Reconect to Conf Portal (iSpindel_Cayenne). 

-You can now enter your Original Gravity (if the iTilt is in the brew) from the Gravity reading 192.168.4.1/readings? . Provide your WiFi password again and save your new details.

-Also change the WiFi Manager portal Time Out to about 300 seconds and the Data Publication Interval to 900 seconds (about 15 minutes).

-Your iTilt should start to publish to Cayenne.

# ENTERING THE WIFIMANAGER CONFIGURATION PORTAL AGAIN
-Short the Reset pin while holding the iTilt vertical

-While Brewing: Draw the iSpindel vertical against the plastic fermenter with a strong magnet. Keep it like that. The iSpindel will detect it is vertical when it wakes out of deep sleep. This may take time, dependent on your selected publication interval.

![conf_wifi](https://user-images.githubusercontent.com/38969599/132992008-7d988ec5-ce01-4246-88a8-0cf3c585161b.gif)


# LED INDICATORS
1 Flashe=Awaike from deep sleep

3 Flashes=Publish data to Cayenne

10 Short flashes= WiFiManager Configuration portal will start in three minutes.


# NOTES ON MODEL CALIBRATION (CALCULATION OF THE POLYNOMIAL)

https://user-images.githubusercontent.com/38969599/134277840-7fbd0afc-ea88-456a-885e-82393b596da0.mp4

-You will need ordered pairs of Measured tilt readings and Gravity readings. Follow the wizard that use a sugar wash calculater to make sure your gravity readings is correct.

-The best polynomial is a 3rd degree of the form GRAVITY=Coefficient3 x Tilt^3+Coefficient2 x Tilt^2+Coefficient1 x Tilt+ConstantTerm (^2 means to the power of 2)

-While calculating your polynomial, you can set the Publication Interval to 0. This will ensure that the ESP8266 (ESP32) does not enter deep sleep and publish data about every 20 seconds to cayenne. 

-You can also get Tilt Readings from the senor reading page 192.168.4.1/readings? Make sure your portal time out(set in Configure WiFi) is long enought (9000 seconds) to finish calibration.

![sensor_readings](https://user-images.githubusercontent.com/38969599/132998146-87ccd30d-14bf-43a2-9055-540e98d72b95.gif)


-If you struggle with the Maths and the wizard, use the following Coefficients Coefficient3=0.000001000000000 Coefficient2=-0.000131373000000 Coefficient1=0.006967952000000 ConstantTerm=0.892383559880001

-Rule of Thumb: If The Polynomial estimates gravity to high, reduce the Coefficient1 a little bit

-If you already have a 2nd degree polynomial, you can use it and just make the coefficient of tilt^3 = 0.

![poly_calibration](https://user-images.githubusercontent.com/38969599/133906857-cb149c17-c7c6-483c-a914-3e74b0a77e5a.gif)
![poly_calibration_data2](https://user-images.githubusercontent.com/38969599/133906854-77b2b2e7-ef9d-4c58-ba83-5df17da5405b.gif)
![poly_calibration_results](https://user-images.githubusercontent.com/38969599/133906856-24bc782c-0fc9-43c5-b623-e3b3608200cb.gif)

My results in Excel using the Data Analysis tool pack. You can see that the results calculated by the Polynomial Calibration Wizard corresponds with excel. You can also see that the Polynomial is closed to a perfect fit with a Coefficient of determination (R^2)=0.9998

![excel](https://user-images.githubusercontent.com/38969599/132998615-d404bfa4-f30b-47f4-ac6a-e90982800261.gif)



# CAYENNE CHANNELS

Channel 0: Constant Term

Channel 1: Coefficient1 x 1000

Channel 2: Coefficient2 x 1000

Channel 3: Coefficient3 x 1000

Channel 4: Temperature

Channel 5: Tilt

Channel 6: Battery Voltage

Channel 7: Gravity (Current)

Channel 8: Publication Interval

Channel 9: Original Gravity

Channel 10: ABV Alcohol By Volume (Original Gravity-Gravity) X 131.258

Channel 11: WiFi Signal Strength (40-50 Excelent, 30-40 Good, 20-30 Reasonable, 10-20 LOW, 0-10 Very Low)

![image](https://user-images.githubusercontent.com/38969599/132103481-0bb79940-bee6-423e-93d6-77c358820f9c.png)

# CHANGE LOG

v1.01

1 Replace the CayenneMQTT library with ArduinoMqttClient. This new library avoids the long loop in CayenneMQTT. The average time it takes to connect to WiFi, publish data and go to deep sleep is now less than 5 seconds.
2 Optimize code, WiFiManager custom params is only created if the portal will run
3 The Polynomial Calibration Wizard is improved. Instructions to use results from sugar wash calculaters is included.
4 The cayenne chanels was updated.

  
  
v1.02

Improve Power management of the DS18B20 and MPU6050. They are only switched on for about a second to take proper readings.
  
# BOARDS.
Get a board that will fit in the PET Tube. Boards must also be low on power consumption during operation and deep sleep. The TTGO Koala looks like the perfect contestant at this stage since iets battery connection is next to the USB port and the charger is braked out to the PINS. Other options may be the DF Robot Fire beetle.
  
The Wemos Lolin32 was tested on a breadboard. The GPIO could not power the MPU6050 and DS18B20 and you will have to power them via the 3.3 volt pin. You may use a transistor to switch them on and of to save power. You will also have to remove the battery conector to fit into your PET tube. Some soldering will be needed to make the battery unit working again.
  
The Sparkfun ESP32 Thing was tested. None of my two boards WiFi worked correctly. You may have better luck with them

# DISCLAIMER
Do not load this firmware if you do not know what you are doing. This firmware over write what is on the iSpindel. It formats a File System (Called SPIFFS) on your EEPROM. If want to revert back to the iSpindel software, you need format your SPIFFS. by loading https://github.com/JJSlabbert/iTilt/blob/main/format_esp_fact_defaults.ino.ino.d1_mini.bin or running https://github.com/JJSlabbert/iTilt/blob/main/format_esp_fact_defaults.ino 

I do not take any responsibility for any damage caused.

# LICENSE
You are free to use this binary or source code (software) for non-commercial objectives, like home brewing for private use. 
You may not include this software in a product that you cell or produce for commercial objectives. 
You may not use this software for producing any commercial product that you cell. 
You are not allowed to distribute this software. The copy right holder is the only legal distributor of this software. 
If you want to use this software commercially, you need to get permission from the copyright owner. 
Commercial Licenses will be available at a reasonable price. 
Copy right holder: Jacobus Johannes Slabbert


