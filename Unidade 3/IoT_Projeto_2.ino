#include <NTPClient.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "DHT.h"
#include "SPIFFS.h"
#include <FS.h>
#include "time.h"
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

#define DHTPIN 4     // Digital pin connected to the DHT sensor
#define DHTTYPE DHT11   // DHT 11
//#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
#define PIN_DOOR 23
#define LED 2

#define BOTtoken "bottoken"
// Use @myidbot to find out the chat ID of an individual or a group
// Also note that you need to click "start" on a bot before it can
// message you
#define CHAT_ID "chat_id"

WiFiClient wifi_client;
PubSubClient mqtt_client(wifi_client);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, -10800);
DHT dht(DHTPIN, DHTTYPE);

WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);

const char* wifi_ssid = "wifi_ssid";
const char* wifi_password = "wifi_password";
int wifi_timeout = 100000;

const char* mqtt_broker = "io.adafruit.com";
const int mqtt_port = 1883;
int mqtt_timeout = 10000;

const char* mqtt_usernameAdafruitIO = "usernamee";
const char* mqtt_keyAdafruitIO = "adafruitkey";

float hum, temp_c, hic = 0;
int doorState;
int anteriorState = 0;
char buffer[10];
unsigned long previousMillis1;  // Variável para armazenar o tempo da última verificação
unsigned long previousMillis2;  // Variável para armazenar o tempo da última ação (para 10 segundos)
const long botRequestDelay = 1000;  
const long adafruitDelay = 10000; // Intervalo de 10 segundos
bool conexionFailed = false;
String filename = "/dados.txt";

void setup() {
  Serial.begin(115200);

  pinMode(PIN_DOOR, INPUT);
  pinMode(LED, OUTPUT);

  WiFi.mode(WIFI_STA); //"station mode": permite o ESP32 ser um cliente da rede WiFi
  WiFi.begin(wifi_ssid, wifi_password);
  connectWiFi();
  openFS();
  timeClient.begin();
  dht.begin();
  mqtt_client.setServer(mqtt_broker, mqtt_port);
}

void loop() {
  timeClient.update();

  unsigned long currentMillis = millis(); 
  
  // Reading temperature or humidity takes about 250 milliseconds!
  // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  hum = dht.readHumidity(); 
  temp_c = dht.readTemperature(); // Read temperature as Celsius (the default) 

  doorState = digitalRead(PIN_DOOR);
  
  // Check if any reads failed and exit early (to try again). 
  if (isnan(hum) || isnan(temp_c)) {
    Serial.println(F("Failed to read from DHT sensor!"));
    delay(400);
    return; 
  }

  // Verifica se houve mudança de estado
  doorStateChange();
  checkTempHum();

  // Compute heat index in Celsius (isFahreheit = false) 
  hic = dht.computeHeatIndex(temp_c, hum, false); 

  // Verificação a cada 1 segundo
  if (currentMillis - previousMillis1 >= botRequestDelay) {
    previousMillis1 = currentMillis;  // Salva o tempo atual

    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

    while(numNewMessages) {
      Serial.println("Bot got response");
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }

  }

  if(currentMillis - previousMillis2 >= adafruitDelay){
    previousMillis2 = currentMillis;  // Salva o tempo atual

    Serial.println(timeClient.getFormattedTime());

    connectMQTT();
    if(!mqtt_client.connected()) { // Se MQTT não estiver conectado
      digitalWrite(LED,LOW);
      bool conexionFailed = true;
      writeDataFile();
    } else {
      digitalWrite(LED, HIGH); // LED indicando conexão

      if(conexionFailed == true){
        conexionFailed = false;
        float mediaTemperatura, mediaUmidade, mediaIndiceCalor;
        readCalculateFile(&mediaTemperatura, &mediaUmidade, &mediaIndiceCalor);
        mqtt_client.publish("matheusriv/feeds/projeto2_umidade", String(mediaUmidade).c_str());
        mqtt_client.publish("matheusriv/feeds/projeto2_temp_c", String(mediaTemperatura).c_str());
        mqtt_client.publish("matheusriv/feeds/projeto2_hic", String(mediaIndiceCalor).c_str());
      }

      Serial.print(F("Umidade: "));
      Serial.print(hum);
      Serial.print(F("%  Temperatura: "));
      Serial.print(temp_c);
      Serial.print(F("°C  Heat Index: "));
      Serial.print(hic);
      Serial.println(F("°C"));

      mqtt_client.publish("matheusriv/feeds/projeto2_umidade", String(hum).c_str());
      mqtt_client.publish("matheusriv/feeds/projeto2_temp_c", String(temp_c).c_str());
      mqtt_client.publish("matheusriv/feeds/projeto2_hic", String(hic).c_str());

      if(doorState == 1){
        mqtt_client.publish("matheusriv/feeds/projeto2_porta", "1");
        Serial.println("Porta: fechada");
      } else{
        mqtt_client.publish("matheusriv/feeds/projeto2_porta", "0");
        Serial.println("Porta: aberta");
      }

      mqtt_client.loop();

      Serial.println("");

    }
  }
}

void checkTempHum(){
  if(temp_c > 35) {
    bot.sendMessage(CHAT_ID, "Alerta: temperatura alta " + String(temp_c, 2), "");
  }
  if(hum < 40) {
    bot.sendMessage(CHAT_ID, "Alerta: humidade baixa " + String(hum, 2), "");
  }
}

void doorStateChange(){
  if(doorState != anteriorState) {
    if(doorState == HIGH) {
      bot.sendMessage(CHAT_ID, "Alerta: porta fechou", "");
      Serial.println("Mudança de 0 para 1");
    } else {
      bot.sendMessage(CHAT_ID, "Alerta: porta abriu", "");
      Serial.println("Mudança de 1 para 0");
    }
    anteriorState = doorState; // Atualiza o estado anterior com o estado atual
  }
}

void writeDataFile() {
  SPIFFS.remove(filename);
  File file = SPIFFS.open(filename, "a");

  if (!file) {
    Serial.println(F("Falha ao abrir o arquivo no SPIFFS!"));
    return;
  }

  // Escreve os dados no arquivo
  file.println(String(temp_c) + "," + String(hum));

  file.close();
}

void readCalculateFile(float *mediaTemperatura, float *mediaUmidade, float *mediaIndiceCalor){
  File file = SPIFFS.open(filename, "r");

  if (!file) {
    Serial.println(F("Falha ao abrir o arquivo no SPIFFS!"));
    return;
  }

  float somaTemperatura = 0.0;
  float somaUmidade = 0.0;
  float somaIndiceCalor = 0.0;
  int numDados = 0;

  // Lê os dados do arquivo e calcula a soma para calcular a média posteriormente
  while (file.available()) {
    String line = file.readStringUntil('\n');
    int commaIndex1 = line.indexOf(',');
    int commaIndex2 = line.indexOf(',', commaIndex1 + 1);

    if (commaIndex1 != -1 && commaIndex2 != -1) {
      float temp = line.substring(0, commaIndex1).toFloat();
      float umid = line.substring(commaIndex1 + 1, commaIndex2).toFloat();
      float heatIndex = line.substring(commaIndex2 + 1).toFloat();

      somaTemperatura += temp;
      somaUmidade += umid;
      somaIndiceCalor += heatIndex;
      numDados++;
    }
  }

  file.close();

  // Calcula a média dos valores
  if (numDados > 0) {
    *mediaTemperatura = somaTemperatura / numDados;
    *mediaUmidade = somaUmidade / numDados;
    *mediaIndiceCalor = somaIndiceCalor / numDados;
  }

}

void readFile(String path) {
  Serial.printf("Lendo arquivo: %s\n", path);

  File file = SPIFFS.open(path, "r");
  if (!file) {
    Serial.println("Não foi possível abrir o arquivo");
    return;
  }

  Serial.print("---------- Lendo arquivo ");
  Serial.print(path);
  Serial.println("  ---------");

  while (file.available()) {
    Serial.write(file.read());
  }
  Serial.println("\n------ Fim da leitura do arquivo -------");
  file.close();
}

void formatFile() {
  Serial.println("Formantando SPIFFS");
  SPIFFS.format();
  Serial.println("Formatou SPIFFS");
}

void openFS(void) {
  if (!SPIFFS.begin(true)) {
    Serial.println("\nErro ao abrir o sistema de arquivos");
  }
  else {
    Serial.println("\nSistema de arquivos aberto com sucesso!");
  }
}

void handleNewMessages(int numNewMessages) {
  Serial.println("handleNewMessages");
  Serial.println(String(numNewMessages));

  for (int i=0; i<numNewMessages; i++) {
    // Chat id of the requester
    String chat_id = String(bot.messages[i].chat_id);
    if (chat_id != CHAT_ID){
      bot.sendMessage(chat_id, "Unauthorized user", "");
      continue;
    }
    
    String text = bot.messages[i].text;
    //Serial.println(text);

    String from_name = bot.messages[i].from_name;

    if(text == "/start") {
      String welcome = "Olá, " + from_name + ".\n";
      welcome += "Use os seguintes comandos para receber mensagens.\n\n";
      welcome += "/temperatura\n";
      welcome += "/umidade\n";
      welcome += "/heat_index\n";
      bot.sendMessage(chat_id, welcome, "");
    }

    if(text == "/temperatura") {
      bot.sendMessage(chat_id, "Temperatura atual: " + String(temp_c, 2) + "°C", "");
    }

    if(text == "/umidade") {
      bot.sendMessage(chat_id, "Humidade atual: " + String(hum, 2) + "%", "");
    }

    if(text == "/heat_index") {
      bot.sendMessage(chat_id, "Heat Index Celsius: " + String(hic, 2) + "%", "");
    }
  }
}

void connectWiFi() {
  Serial.print("Conectando à rede WiFi .. ");

  unsigned long tempoInicial = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - tempoInicial < wifi_timeout)) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  #ifdef ESP32
  client.setCACert(TELEGRAM_CERTIFICATE_ROOT); // Add root certificate for api.telegram.org
  #endif

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
      Serial.println(" Conectado!");
    } else {
      Serial.println(" Conexão falhou!");
      delay(200);
    }
  }
  Serial.println();
}