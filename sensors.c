#include <WiFiNINA.h>
#include <PubSubClient.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <DHT.h>
#include <ArduinoMqttClient.h>
#include <WiFiNINA.h>

const char* ssid = "Vicky";
const char* password = "Vicky124";

const char* ntpServer = "0.pool.ntp.org";
const char* mqtt_server = "172.20.10.5";

const char broker[] = "broker.hivemq.com";
int port = 1883;
const char topic1[] = "shms";
const char topic[] = "shms/rec";

volatile bool buttonPressed = false;

const byte interruptPin = 2;
const byte buzzer = 9;
bool buttonstate = 0;

WiFiClient wifiClient;
PubSubClient client(wifiClient);
MqttClient mqttClient(wifiClient);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer);

#define DHTPIN 3
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

MAX30105 particleSensor;

const byte RATE_SIZE = 4; // Increase this for more averaging. 4 is good.
byte rates[RATE_SIZE]; // Array of heart rates
byte rateSpot = 0;
long lastBeat = 0; // Time at which the last beat occurred
long lastTransmitTime = 0; // Time of the last data transmission

float beatsPerMinute;
int beatAvg;

float humidity;
float temperature;

void setup()
{
  timeClient.begin();
  Serial.begin(9600);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  client.setServer(mqtt_server, 1884);
  while (!client.connected()) {
    Serial.println("Connecting to MQTT broker...");
    if (client.connect("arduino")) {
      Serial.println("Connected to MQTT broker");
    } else {
      Serial.print("Failed with state ");
      Serial.print(client.state());
      delay(2000);
    }
  }
  dht.begin();

  pinMode(buzzer, OUTPUT);
  pinMode(interruptPin, INPUT_PULLUP);
  Serial.begin(9600);

  attachInterrupt(digitalPinToInterrupt(interruptPin), handleButtonPress, RISING);

  while (!Serial) {
    // wait for serial port to connect
  }

  Serial.print("Attempting to connect to WPA SSID: ");
  Serial.println(ssid);
  while (WiFi.begin(ssid, password) != WL_CONNECTED) {
    Serial.print(".");
    delay(5000);
  }

  Serial.println("You're connected to the network");
  Serial.println();

  Serial.print("Attempting to connect to the MQTT broker: ");
  Serial.println(broker);

  if (!mqttClient.connect(broker, port)) {
    Serial.print("MQTT connection failed! Error code = ");
    Serial.println(mqttClient.connectError());
    while (1);
  }

  Serial.println("You're connected to the MQTT broker!");
  mqttClient.subscribe(topic);
  Serial.println();

  Serial.println("Initializing...");

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 was not found. Please check wiring/power. ");
    while (1);
  }
  Serial.println("Place your index finger on the sensor with steady pressure.");

  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeGreen(0);

  lastTransmitTime = millis(); // Initialize the last transmission time
}

void loop()
{
  timeClient.update();
  humidity = dht.readHumidity();
  temperature = dht.readTemperature();

  int messageSize = mqttClient.parseMessage();
  if (messageSize) {
    // use the Stream interface to print the contents
    while (mqttClient.available()) {
      Serial.print((char)mqttClient.read());
      buttonstate = 1;
    }


  }
  if (buttonstate == 1) {
    digitalWrite(buzzer, HIGH);
    delay(3000);
    digitalWrite(buzzer, LOW);
    buttonstate = 0;
  }

  long irValue = particleSensor.getIR();

  if (checkForBeat(irValue) == true) {
    long delta = millis() - lastBeat;
    lastBeat = millis();
    beatsPerMinute = 60 / (delta / 1000.0);

    if (beatsPerMinute < 255 && beatsPerMinute > 20) {
      rates[rateSpot++] = (byte)beatsPerMinute;
      rateSpot %= RATE_SIZE;
      beatAvg = 0;
      for (byte x = 0 ; x < RATE_SIZE ; x++)
        beatAvg += rates[x];
      beatAvg /= RATE_SIZE;
    }
  }

  if (millis() - lastTransmitTime >= 4000) {
    String date1 = timeClient.getFormattedTime();

    Serial.print("Humidity: ");
    Serial.print(humidity);
    Serial.print(" %, Temp: ");
    Serial.print(temperature);
    Serial.println(" Celsius");

    client.publish("humidity", String(humidity).c_str());
    client.publish("temperature", String(temperature).c_str());

    Serial.print("IR=");
    Serial.print(irValue);
    Serial.print(", BPM=");
    Serial.print(beatsPerMinute);
    Serial.print(", Avg BPM=");
    Serial.print(beatAvg);

    if (irValue < 50000)
      Serial.print(" No finger?");

    Serial.println();
    client.publish("IR", String(irValue).c_str());
    client.publish("BPM", String(beatsPerMinute).c_str());
    client.publish("Avg_BPM", String(beatAvg).c_str());

    Serial.print("Current time: ");
    Serial.println(date1);

    client.publish("date", date1.c_str(), false);

    lastTransmitTime = millis(); // Update the last transmission time
  }
}

void handleButtonPress() {
  mqttClient.poll();
  mqttClient.beginMessage(topic1);
  mqttClient.print("Emergency !!!! ");
  mqttClient.endMessage();
  buttonstate = 1;
}