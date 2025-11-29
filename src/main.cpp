#include <Arduino.h>
#include <WiFi.h>
#include <DHT.h>
#include <DHT_U.h>
#include <PubSubClient.h>
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>


#define DHTPIN        4 
#define waterpump     18
#define waterpin      23 //botão de acionamento manual da bomba d'água
#define soilPin       34
#define ExaustFan     19
#define CoolingFan    21
#define HeatingLed    22 //representaçâo de aquecedor
#define LDRPin        32


#define DHTTYPE DHT11   
DHT dht(DHTPIN, DHTTYPE);

#if defined(ESP32)
#include <WiFiMulti.h>
WiFiMulti wifiMulti;
#define DEVICE "ESP32"
#elif defined(ESP8266)
#include <ESP8266WiFiMulti.h>
ESP8266WiFiMulti wifiMulti;
#define DEVICE "ESP8266"
#endif



// WiFi AP SSID
#define WIFI_SSID         "Gustavo"
#define WIFI_PASSWORD     "mqtteste"

#define INFLUXDB_URL "https://us-east-1-1.aws.cloud2.influxdata.com"
#define INFLUXDB_TOKEN "qaAijaDITMPeRl6EkKtBmzx1mAt-H9UeG71jjAPmRioTIUlmP2Y0N_GnFgY1M4YXAAapc1QjUWcIlFPRUktvvA=="
#define INFLUXDB_ORG "ac342d73b369637c"
#define INFLUXDB_BUCKET "Estufa"
//#define DEVICE            "estufa_001"
#define INFLUXDB_SEND_TIME    (5000u) //10s
#define DHT11_REFRESH_TIME    (2000u) //5s
#define SENSOR_BUFFER_SIZE    (INFLUXDB_SEND_TIME/DHT11_REFRESH_TIME)
// Time zone info
#define TZ_INFO           "UTC-3"

// Declare InfluxDB client instance with preconfigured InfluxCloud certificate
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

// Declare Data point
Point sensor("ESTUFA");

static uint32_t dht_refresh_timestamp = 0u;
static uint32_t influxdb_send_timestamp = 0u;
static void WiFi_Setup( void );
static void DHT11_TaskInit( void );
static void Measures( void );
static void InfluxDB_TaskInit( void );
static void InfluxDB_TaskMng( void );
static float Get_HumidityValue( void );
static float Get_TemperatureValue( void );
static float Get_MoistureValue( void );
static float Get_LuminosityValue( void );

// Function to calculate the average moisture value
// Remove this duplicate function definition as it is already defined earlier in the code.

//variaveis
float dht_temperature = 0;
float dht_humidity = 0;
float moist_measure = 0;
float Luminosity = 0;

const float GAMMA = 0.7;
const float RL10 = 33;


//float Moisture = 0;
static float temp_buffer[SENSOR_BUFFER_SIZE] = { 0 };
static float humidity_buffer[SENSOR_BUFFER_SIZE] = { 0 };
static float Moist_buffer[SENSOR_BUFFER_SIZE] = { 0 };
static float Lumi_buffer[SENSOR_BUFFER_SIZE] = { 0 };
static uint16_t sensor_buffer_idx = 0;


IRAM_ATTR void ManualWatering(){
  digitalWrite(waterpump, HIGH);
  delay(1000);
  digitalWrite(waterpump, LOW);
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  WiFi_Setup();
  InfluxDB_TaskInit();
  DHT11_TaskInit();

  pinMode(waterpump, OUTPUT);
  pinMode(soilPin, INPUT);
  pinMode(ExaustFan, OUTPUT);
  pinMode(CoolingFan, OUTPUT);
  
  attachInterrupt(waterpin, ManualWatering, FALLING);

 sensor.addTag("SSID", WiFi.SSID());
  
}
void loop() {
  Measures();
  InfluxDB_TaskMng();
  digitalWrite(CoolingFan,HIGH);
  delay(1000);
  digitalWrite(CoolingFan,LOW);

  /*
  if(moist_measure<69){// aproximadamente 35% de umidade do solo
    digitalWrite(waterpump, HIGH);
  }
  else{
    digitalWrite(waterpump, LOW);
  }

  if(dht_humidity > 60){
    digitalWrite(ExaustFan, HIGH);
    digitalWrite(CoolingFan,LOW);
    digitalWrite(HeatingLed, LOW);

  }

  if(dht_temperature > 24){
    digitalWrite(CoolingFan, HIGH);
  }

  if(dht_temperature < 18){
    digitalWrite(ExaustFan, LOW);
    digitalWrite(CoolingFan, LOW);
    digitalWrite(HeatingLed, HIGH);
  }
  */
}
static void WiFi_Setup(void){
   // Setup wifi
  Serial.println("Connecting to wifi");
  WiFi.mode(WIFI_STA);
  wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);

  while (wifiMulti.run() != WL_CONNECTED) {
    Serial.print(".");
    delay(100);
  }
  Serial.println("Connected to WiFi");
  Serial.println();
}
static void DHT11_TaskInit( void ){
  dht.begin();
  // delay(2000);
  dht_refresh_timestamp = millis();
}
static void Measures( void ){
   uint32_t now = millis();
  float temperature, humidity, moist, Light;

  if( now - dht_refresh_timestamp >= DHT11_REFRESH_TIME ){
    dht_refresh_timestamp = now;
    humidity = dht.readHumidity();
    temperature = dht.readTemperature();
    moist = analogRead(soilPin); 
    Light = analogRead(LDRPin);

    float voltage = Light / 4096. * 3.3;
    float resistance = 2000 * voltage / (1 - voltage / 3.3);
    float lux = pow(RL10 * 1e3 * pow(10, GAMMA) / resistance, (1 / GAMMA));

    if (isnan(humidity) || isnan(temperature) ) 
    {
      Serial.println(F("Failed to read from DHT sensor!"));
    }
    else
    {
      Serial.print(F("Humidity: "));
      Serial.print(humidity);
      Serial.print(F("%  Temperature: "));
      Serial.print(temperature);
      Serial.println(F("°C "));
      Serial.print(F("Soil Moisture: "));
      Serial.println(moist_measure);
      Serial.print(F("Luminosity: "));
      Serial.println(Luminosity);
      // store this in the global variables
      dht_humidity = humidity;
      dht_temperature = temperature;
      moist_measure = moist/4095.0*100.0; // Convert to percentage
      //Luminosity = (lux/4095.0)*100.0; // Convert to percentage
      temp_buffer[sensor_buffer_idx] = dht_temperature;
      humidity_buffer[sensor_buffer_idx] = dht_humidity;
      Moist_buffer[sensor_buffer_idx] = moist_measure;
      Lumi_buffer[sensor_buffer_idx] = lux;
      sensor_buffer_idx++;
      if( sensor_buffer_idx >= SENSOR_BUFFER_SIZE )
      {
        sensor_buffer_idx = 0;
      }
    }
  }
}
static void InfluxDB_TaskInit(void){
  sensor.addTag("device", DEVICE);
  timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");
  // teste de conexão com o influxdb
   if (client.validateConnection()) {
    Serial.print("Connected to InfluxDB: ");
    Serial.println(client.getServerUrl());
  } else {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(client.getLastErrorMessage());
  }
}
static void InfluxDB_TaskMng(void){
  uint32_t now = millis();
  if( now - influxdb_send_timestamp >= INFLUXDB_SEND_TIME )
  {
    influxdb_send_timestamp = now;
   
    sensor.clearFields();
    sensor.addField("rssi", WiFi.RSSI());
    sensor.addField("temperature", Get_TemperatureValue());
    sensor.addField("humidity", Get_HumidityValue());
    sensor.addField("moisture",Get_MoistureValue());
    sensor.addField("luminosity",Get_LuminosityValue());

    Serial.print("Moisture luminosidade: ");
    Serial.println(Get_LuminosityValue());

    // Print what are we exactly writing
    Serial.print("Writing: ");
    Serial.println(client.pointToLineProtocol(sensor));
    // If no Wifi signal, try to reconnect it
    if (wifiMulti.run() != WL_CONNECTED)
    {
        Serial.println("Wifi connection lost");
    }
    // Write point
    if (!client.writePoint(sensor))
    {
      Serial.print("InfluxDB write failed: ");
      Serial.println(client.getLastErrorMessage());
    }
  }
}

static float Get_HumidityValue(void) {
    uint16_t idx = 0u;
    float temp = 0;
    for (idx = 0; idx < SENSOR_BUFFER_SIZE; idx++) {
        temp = temp + humidity_buffer[idx];
    }
    temp = temp / SENSOR_BUFFER_SIZE;
    return temp;
}

static float Get_TemperatureValue(void) {

    uint16_t idx = 0u;
    float temp = 0;
    for (idx = 0; idx < SENSOR_BUFFER_SIZE; idx++) {
        temp = temp + temp_buffer[idx];
    }
    temp = temp / SENSOR_BUFFER_SIZE;
    return temp;
}


static float Get_MoistureValue(void) {
 uint16_t idx = 0u;
    float temp = 0;
    for (idx = 0; idx < SENSOR_BUFFER_SIZE; idx++) {
        temp = temp + Moist_buffer[idx];
    }
    temp = temp / SENSOR_BUFFER_SIZE;
    return temp;
}

static float Get_LuminosityValue(void) {
 uint16_t idx = 0u;
    float temp = 0;
    for (idx = 0; idx < SENSOR_BUFFER_SIZE; idx++) {
        temp = temp + Lumi_buffer[idx];
    }
    temp = temp / SENSOR_BUFFER_SIZE;
    return temp;
}
