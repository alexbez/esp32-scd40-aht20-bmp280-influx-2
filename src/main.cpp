#include <Arduino.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include <SensirionI2CScd4x.h>
#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>

#include  "credentials.h"

#define SERIAL_SPEED  115200
#define MEASURE_DELAY 60000 // 10 sec
#define BUFFER_SIZE 80

#define TZ_INFO "EET,M3.5.0,M10.5.0/3"
#define TIMESERVER1 "pool.ntp.org"
#define TIMESERVER2 "time.nis.gov"

Adafruit_BMP280 bmp; 
Adafruit_AHTX0 aht;
SensirionI2CScd4x scd4x;
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert); // INFLUXDB_xxx are from credentials.h
Point datapoint(INFLUXDB_MEASUREMENT);


void setup() {

  Serial.begin(SERIAL_SPEED);
  Serial.println("\nSCD40-AHT20-BMP280 Test");

  Wire.begin();

  if(!aht.begin())
  {
    Serial.println("Cannot initialize AHT20 sensor");
    while(true);
  }
  Serial.println("AHT20  sensor initialized OK");

  if(!bmp.begin())
  {
    Serial.println("Cannot initialize BMP280 sensor");
    while(true) ;
  }
  Serial.println("BMP280 sensor initialized OK");

   uint16_t error;
    char errorMessage[256];

    scd4x.begin(Wire);

    // stop potentially previously started measurement
    error = scd4x.stopPeriodicMeasurement();
    if (error) {
        Serial.print("Error trying to execute stopPeriodicMeasurement(): ");
        errorToString(error, errorMessage, 256);
        Serial.println(errorMessage);
    }
    error = scd4x.startPeriodicMeasurement();
    if (error) {
        Serial.print("Error trying to execute startPeriodicMeasurement(): ");
        errorToString(error, errorMessage, 256);
        Serial.println(errorMessage);
    }
    Serial.println("SCD40 sensor initialized OK");

    Serial.print("Connecting to WiFi: ");
    Serial.print(WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWD);

  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    Serial.print(".");
  }
  Serial.print("OK, IP=");
  Serial.println(WiFi.localIP());

  if (client.validateConnection()) {
    Serial.print("Connected to InfluxDB: ");
    Serial.println(client.getServerUrl());
  } else {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(client.getLastErrorMessage());
    for(;;) {}
  }
  timeSync(TZ_INFO, TIMESERVER1, TIMESERVER2);
  Serial.println("Time synced with network time servers");
  
  randomSeed(micros());
    Serial.println("Waiting for first measurement... (5 sec)");
    delay(5000);
}

void loop() {
  uint16_t error;
  char errorMessage[256];
  bool isDataReady = false;
  float temp1, temp2, temp3, humidity1, humidity2, pressure;
  uint16_t co2 = 0;
  sensors_event_t humidity_event, temp_event;

  float avg_temp = 0.0;
  float avg_humidity = 0.0;

  error = scd4x.getDataReadyFlag(isDataReady);

  if (!isDataReady) {
      Serial.println("SCD40 data NOT ready");
  }
  else
  {
    error = scd4x.readMeasurement(co2, temp3, humidity2);
    if (error) {
        Serial.print("Error trying to execute readMeasurement(): ");
        errorToString(error, errorMessage, 256);
        Serial.println(errorMessage);
    } else if (co2 == 0) {
        Serial.println("Invalid sample detected, skipping.");
    } else {
  #ifdef  DEBUG
        Serial.print("SCD40: CO2=");
        Serial.print(co2);
        Serial.print("ppm\t");
        Serial.print("T=");
        Serial.print(temp3);
        Serial.print("C\t");
        Serial.print("H=");
        Serial.print(humidity2);
        Serial.println("%");
#endif
    }
  }

  temp1 = bmp.readTemperature();
  pressure = bmp.readPressure() * 0.75006 / 100;
#ifdef  DEBUG
  Serial.print("BMP280: T=");
  Serial.print(temp1);
  Serial.print("C, P=");
  Serial.print(pressure);
  Serial.println("mm.Hg");
#endif
  aht.getEvent(&humidity_event, &temp_event);
  temp2 = temp_event.temperature;
  humidity1 = humidity_event.relative_humidity;
  
#ifdef DEBUG
  Serial.print("AHT20 : T=");
  Serial.print(temp2);
  Serial.print("C, H=");
  Serial.print(humidity1);
  Serial.println("%\n");
#endif
  if(isDataReady)
  {
    avg_temp = (temp1 + temp2 + temp3) / 3.0;
    avg_humidity = (humidity1 + humidity2) / 2.0;
  }
  else
  {
    avg_temp = (temp1 + temp2) / 2.0;
    avg_humidity = humidity1;
  }

  avg_temp = round(avg_temp * 10) / 10;
  avg_humidity = round(avg_humidity);
  pressure = round(pressure);
  
  Serial.print(avg_temp); Serial.print("C, ");
  Serial.print(avg_humidity); Serial.print("%, ");
  Serial.print(pressure); Serial.print("mm.Hg");
  if(isDataReady)
  {
    Serial.print(", ");
    Serial.print(co2);
    Serial.print("ppm");
  }

  datapoint.clearFields();
  datapoint.addField("temperature", avg_temp);
  datapoint.addField("humidity", avg_humidity);
  datapoint.addField("pressure", pressure);
  if(isDataReady)
  {
    datapoint.addField("co2", co2);
  }
  
  if (!client.writePoint(datapoint)) 
  {
    Serial.print("   InfluxDB write failed: ");
    Serial.println(client.getLastErrorMessage());
  }
  else
  {
    Serial.println("   Data written to InfluxDB");
  }

  delay(MEASURE_DELAY);
   
}
