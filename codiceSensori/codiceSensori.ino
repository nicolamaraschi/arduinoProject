#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ArduinoJson.h>

const char* ssid = "FASTWEB-PXPSTE";
const char* password = "43LY9LDLPH";
const char* mqttServer = "pissir.colnet.rocks";
const int mqttPort = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

const int echoPin = D5;  // Echo
const int trigPin = D6;  // Trig
long duration;
int distance;

int humidityValue = 0; // Variabile per memorizzare l'umidità come intero

// Calibrazione sensori volumetrici
float distances[] = {11, 9,  6.5, 4, 2.6 };
float volumes[] =   {0, 150.0, 300.0,  450.0, 600};

#define DHTPIN1 D2
#define DHTPIN2 D4
#define DHTTYPE DHT11
DHT dht1(DHTPIN1, DHTTYPE);
DHT dht2(DHTPIN2, DHTTYPE);
float calculateVolume(float distance);

const int soilMoisturePin = A0;

bool sensorsAnnounced = false;

void setup() {
  Serial.begin(9600);
  // per umidita seriale devi scegliere tra seriale 9600 e 
  Serial1.begin(9600);

  // Connessione WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connesione al WiFi in corso...");
  }
  Serial.println("Adesso Connesso al WiFi");

  // Inizializzazione del client MQTT
  client.setServer(mqttServer, mqttPort);

  // Inizializzazione del sensore DHT
  dht1.begin();
  dht2.begin();

  // Impostazione dei pin per ultrasuoni
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);


  delay(5000);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (!sensorsAnnounced) {
    announceSensors();
    sensorsAnnounced = true; // Imposta il flag su true per non eseguire più volte
  }

  if (client.connected()) {
    // Ultrasuoni
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    duration = pulseIn(echoPin, HIGH);
    distance = duration * 0.034 / 2;
    Serial.print("Distance: ");
    Serial.println(distance);
    
   // Calcola il volume utilizzando la funzione
  float totalVolume = calculateVolume(distance);

  Serial.print("Total Volume: ");
  Serial.print(totalVolume);
  Serial.println(" ml");

  client.publish("/livelloAcqua", createMessage("livelloAcquaEsp1", totalVolume).c_str());

    // Temperatura e umidità
    float temp1 = dht1.readTemperature();
    float humidity1 = dht1.readHumidity();
    float temp2 = dht2.readTemperature();
    float humidity2 = dht2.readHumidity();
    Serial.print("Temperature: ");
    Serial.println(temp1);
    Serial.print("Humidity: ");
    Serial.println(humidity1);
    client.publish("/temperaturaAria", createMessage("tempAria1Esp1", temp1).c_str());
    client.publish("/umiditaAria", createMessage("umidAria1Esp1", humidity1).c_str());
    client.publish("/temperaturaAria", createMessage("tempAria2Esp1", temp2).c_str());
    client.publish("/umiditaAria", createMessage("umidAria2Esp1", humidity2).c_str());

    // Sensore umidità terreno
    int soilMoisture = analogRead(soilMoisturePin);
    Serial.print("Soil Moisture: ");
    Serial.println(soilMoisture);
    client.publish("/umiditaTerreno", createMessage("umidTerreno1Esp1", soilMoisture).c_str());
    //client.publish("/umiditaTerreno", createMessage("umidTerreno2Esp1", soilMoisture).c_str());
    
    if (Serial1.available()) {
    // Converti la stringa in un valore float
    humidityValue = data.toFloat();
    // Ora puoi fare quello che vuoi con humidityValue, ad esempio stamparlo
    Serial.print("Valore di umidità ricevuto: ");
    Serial.println(humidityValue);
    client.publish("/umiditaTerreno", createMessage("umidTerreno2Esp1", humidityValue).c_str());
  }
    
  }

  delay(60000);
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT server...");
    if (client.connect("ESP8266Client")) {
      Serial.println("Connected to MQTT server");
    } else {
      Serial.print("Retrying in 5 seconds...");
      delay(5000);
    }
  }
}

String createMessage(const char* nome, float valore) {
  // Creazione del messaggio JSON
  StaticJsonDocument<200> jsonDoc;
  jsonDoc["nome"] = nome;
  jsonDoc["valore"] = valore;
  String jsonStr;
  serializeJson(jsonDoc, jsonStr);
  return jsonStr;
}

void announceSensor(const char* tipoSensore, const char* nomeSensore, int idCampo) {
  // Annuncio del sensore nel topic /announce
  StaticJsonDocument<200> jsonDoc;

  jsonDoc["tipoSensore"] = tipoSensore;
  jsonDoc["nome"] = nomeSensore;
  jsonDoc["idCampo"] = idCampo; // Aggiunta del termine campo come intero
  String jsonStr;
  serializeJson(jsonDoc, jsonStr);
  client.publish("/announce", jsonStr.c_str());
}

void announceSensors() {
  // Annuncio dei sensori al topic /announce
  announceSensor("temperaturaAria", "tempAria1Esp1", 1);
  announceSensor("temperaturaAria", "tempAria2Esp1", 2);
  announceSensor("umiditaAria", "umidAria1Esp1", 1);
  announceSensor("umiditaAria", "umidAria2Esp1", 2);
  announceSensor("livelloAcqua", "livelloAcquaEsp1", 1);
  announceSensor("umiditaTerreno", "umidTerreno1Esp1", 1);
  announceSensor("umiditaTerreno", "umidTerreno2Esp1", 2);
}

// Funzione per calcolare il volume basato su una regressione lineare
float calculateVolume(float distance) {
  if (distance >= distances[0]) {
    return volumes[0];  // Serbatoio vuoto
  } else if (distance <= distances[3]) {
    return volumes[3];  // 650 ml
  } else {
    // Calcola la retta di regressione lineare
    float slope = (volumes[3] - volumes[0]) / (distances[3] - distances[0]);
    float intercept = volumes[0] - slope * distances[0];
    
    // Calcola il volume utilizzando la retta
    return slope * distance + intercept;
  }
}


