#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include "DHT.h"
#include <FS.h>
#include "SPIFFS.h"
#include <TimeLib.h>

#define DHTPIN 4     // Digital pin connected to the DHT sensor
#define DHTTYPE DHT11   // DHT 11
//#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
#define PIN_DOOR 23
#define LED 2

WiFiClient wifi_client;
PubSubClient mqtt_client(wifi_client);

const char* wifi_ssid = "wifi_ssid";
const char* wifi_password = "wifi_password";
int wifi_timeout = 100000;

const char* mqtt_broker = "io.adafruit.com";
const int mqtt_port = 1883;
int mqtt_timeout = 10000;

const char* mqtt_usernameAdafruitIO = "user";
const char* mqtt_keyAdafruitIO = "keyAdafruit";

DHT dht(DHTPIN, DHTTYPE);

int valor = 0;

void setup() {
  Serial.begin(115200);
  pinMode(PIN_DOOR, INPUT);
  pinMode(LED, OUTPUT);
  dht.begin();
  WiFi.mode(WIFI_STA); //"station mode": permite o ESP32 ser um cliente da rede WiFi
  WiFi.begin(wifi_ssid, wifi_password);
  connectWiFi();
  mqtt_client.setServer(mqtt_broker, mqtt_port);
  if (!SPIFFS.begin(true)) {
    Serial.println("Falha ao montar o sistema de arquivos SPIFFS");
    return;
  }
}

void loop() {
  if (!mqtt_client.connected()) { // Se MQTT não estiver conectado
    digitalWrite(LED,LOW);
    connectMQTT();
  }

  if (mqtt_client.connected()) {
    digitalWrite(LED, HIGH);
    
    // Reading temperature or humidity takes about 250 milliseconds!
    // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
    float h = dht.readHumidity();
    // Read temperature as Celsius (the default)
    float t = dht.readTemperature();
    // Read temperature as Fahrenheit (isFahrenheit = true)
    float f = dht.readTemperature(true);
    // Check if any reads failed and exit early (to try again).
    if (isnan(h) || isnan(t) || isnan(f)) {
    	Serial.println(F("Failed to read from DHT sensor!"));
      delay(400);
    	return;
    }
    
    // Compute heat index in Fahrenheit (the default)
    float hif = dht.computeHeatIndex(f, h);
    // Compute heat index in Celsius (isFahreheit = false)
    float hic = dht.computeHeatIndex(t, h, false);
    /*Serial.print(F("Humidity: "));
    Serial.print(h);
    Serial.print(F("%  Temperature: "));
    Serial.print(t);
    Serial.print(F("°C "));
    Serial.print(f);
    Serial.print(F("°F  Heat index: "));
    Serial.print(hic);
    Serial.print(F("°C "));
    Serial.print(hif);
    Serial.println(F("°F"))*/

    /*float valor_um = random(2000, 8000) / 100.0;
    float valor_c = random(2000, 4000) / 100.0;
    float valor_hic = random(0, 10000) / 100.0;*/
    mqtt_client.publish("user/feeds/project_umidade", String(h).c_str());
    Serial.println("Publicou o dado - umidade: " + String(h));
    mqtt_client.publish("user/feeds/project_temp_c", String(t).c_str());
    Serial.println("Publicou o dado - celsius: " + String(t));
    mqtt_client.publish("user/feeds/project_hic", String(hic).c_str());
    Serial.println("Publicou o dado - hic: " + String(hic));
    
    if(digitalRead(PIN_DOOR) == 1){
      mqtt_client.publish("user/feeds/project_botao", "1");
      Serial.println("Publicou o dado - porta: 1");
    } else{
      mqtt_client.publish("user/feeds/project_botao", "0");
      Serial.println("Publicou o dado - porta: 0");
    }
    mqtt_client.loop();
    Serial.println("");
    
    delay(10000);

  }
}

void adicionarDadosAoArquivoDHT(float h, float t, float f, float hic, float hif) {
  File file = SPIFFS.open("/dados_sensor_dht.txt", "a");
  if (!file) {
    Serial.println(F("Falha ao abrir o arquivo no SPIFFS!"));
    return;
  }

  // Obter a hora atual
  //time_t now = time(nullptr);

  // Formatar a hora atual como uma string
  //char timestamp[20]; // Tamanho suficiente para a data e hora formatada
  //snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02d %02d:%02d:%02d",
  //         year(now), month(now), day(now), hour(now), minute(now), second(now));

  // Escrever o timestamp no arquivo
  //file.print(timestamp);

  file.print(" Humidade: ");
  file.print(h);
  file.print("%  Temperatura: ");
  file.print(t);
  file.print("°C ");
  file.print(f);
  file.print("°F  Índice de calor: ");
  file.print(hic);
  file.print("°C ");
  file.print(hif);
  file.println("°F");

  file.close();
}

void lerArquivo() {
  File file = SPIFFS.open("/dados_sensor.txt", "r");
  if (!file) {
    Serial.println(F("Falha ao abrir o arquivo para leitura!"));
    return;
  }

  Serial.println(F("Conteúdo do arquivo:"));
  while (file.available()) {
    Serial.write(file.read());
  }
  Serial.println("Fim do arquivo");

  file.close();
}

void connectWiFi() {
  Serial.print("Conectando à rede WiFi .. ");

  unsigned long tempoInicial = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - tempoInicial < wifi_timeout)) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Conexão com WiFi falhou!");
  } else {
    Serial.print("Conectado com o IP: ");
    Serial.println(WiFi.localIP());
  }
}

void connectMQTT() {
  unsigned long tempoInicial = millis();
  while (!mqtt_client.connected() && (millis() - tempoInicial < mqtt_timeout)) {
    if (WiFi.status() != WL_CONNECTED) {
      connectWiFi();
    }
    Serial.print("Conectando ao MQTT Broker..");

    if (mqtt_client.connect("ESP32Client", mqtt_usernameAdafruitIO, mqtt_keyAdafruitIO)) {
      Serial.println();
      Serial.print("Conectado ao broker MQTT!");
    } else {
      Serial.println();
      Serial.print("Conexão com o broker MQTT falhou!");
      delay(500);
    }
  }
  Serial.println();
}
