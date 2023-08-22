#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <TaskScheduler.h>
#include <NTPClient.h>
#include <EEPROM.h>
#include <WiFiUdp.h>

const int utcOffsetInSeconds = 3600; // Offset orario (in secondi) dalla UTC (ad esempio, 1 ora)
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);

// Impostazioni server NTP
const char* ntpServerName = "pool.ntp.org";

const char* ssid = "FASTWEB-PXPSTE";
const char* password = "43LY9LDLPH";
const char* mqttServer = "pissir.colnet.rocks";
const int mqttPort = 1883;
const char* actionTopic = "/action";
bool announced = false;

WiFiClient espClient;
PubSubClient client(espClient);

const int relayD2Pin = 4;
const int relayD3Pin = 0;
const int relayD4Pin = 2;
const int relayD5Pin = 14;

bool relay1State = false;
bool relay2State = false;
bool relay3State = false;
bool relay4State = false;

// Inserisci questa dichiarazione prima della funzione setup()
struct ProgrammaIrrigazione;

void erogaAcqua(const char* idAttuatore, float quantita);
void erogaMedio(const char* idAttuatore, float quantita);
void erogaBasso(const char* idAttuatore, float quantita);
void callback(char* topic, byte* payload, unsigned int length);
void callbackWrapper();
void erogaAcquaWrapper();
void erogaMedioWrapper();
void erogaBassoWrapper();

Task callbackTask(0, TASK_ONCE, &callbackWrapper);
Task erogaAcquaTask(0, TASK_ONCE, &erogaAcquaWrapper);
Task erogaMedioTask(0, TASK_ONCE, &erogaMedioWrapper);
Task erogaBassoTask(0, TASK_ONCE, &erogaBassoWrapper);

Scheduler scheduler;

void callbackWrapper() {
  callback(NULL, NULL, 0);  // Chiamata a callback senza argomenti
}

void erogaAcquaWrapper() {
  erogaAcqua(NULL, 0.0);  // Chiamata a erogaAcqua senza argomenti
}

void erogaMedioWrapper() {
  erogaMedio(NULL, 0.0);  // Chiamata a erogaMedio senza argomenti
}

void erogaBassoWrapper() {
  erogaBasso(NULL, 0.0);  // Chiamata a erogaBasso senza argomenti
}


// EEPROM
struct ProgrammaIrrigazione {
  char tipoIrrigazione[20];
  char idAttuatore[20];
  float quantita;
  int ora;
  int minuto;
};

const int EEPROM_SIZE = sizeof(ProgrammaIrrigazione);
const int NUMERO_PROGRAMMI_IRRIGAZIONE = 10; // Numero massimo di programmi di irrigazione
const int EEPROM_ADDRESS = 0; // Indirizzo di inizio EEPROM per i dati di irrigazione

void setup() {
  Serial.begin(9600);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);

  pinMode(relayD2Pin, OUTPUT);
  pinMode(relayD3Pin, OUTPUT);
  pinMode(relayD4Pin, OUTPUT);
  pinMode(relayD5Pin, OUTPUT);

  digitalWrite(relayD2Pin, LOW);
  digitalWrite(relayD3Pin, LOW);
  digitalWrite(relayD4Pin, LOW);
  digitalWrite(relayD5Pin, LOW);

  // Assegna i tuoi Task ai relativi periodi di esecuzione
  scheduler.addTask(erogaAcquaTask);
  scheduler.addTask(erogaMedioTask);
  scheduler.addTask(erogaBassoTask);
  scheduler.addTask(callbackTask); // Aggiungi il task callbackTask

  // Inizializza la libreria NTPClient
  timeClient.begin();
  timeClient.setTimeOffset(utcOffsetInSeconds);

  // Recupera l'ora corrente dal server NTP
  while (!timeClient.update()) {
    timeClient.forceUpdate();
  }

  // Inizia il loop di scheduling
  scheduler.startNow();
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Esegui i task del scheduler
  scheduler.execute();

  // Esegui announceAttuatoris() solo una volta
  if (!announced) {
    announceAttuatoris();
    announced = true; // Imposta la variabile a true per evitare esecuzioni successive
  }

  // Recupera l'ora corrente dal server NTP
  timeClient.update();
  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();

  // Confronta l'orario corrente con i programmi di irrigazione salvati in EEPROM
  for (int i = 0; i < NUMERO_PROGRAMMI_IRRIGAZIONE; i++) {
    ProgrammaIrrigazione programma;
    EEPROM.get(EEPROM_ADDRESS + i * EEPROM_SIZE, programma);

    if (currentHour == programma.ora && currentMinute == programma.minuto) {
      // Esegui il codice di irrigazione per questo programma
      eseguiCodiceIrrigazione(programma);
    }
  }
}



void callback(char* topic, byte* payload, unsigned int length) {
  Serial.println("Message arrived in topic: " + String(topic));

  // Verifica se il messaggio è sul topic di comando /action
  if (strcmp(topic, actionTopic) == 0) {
    // Converti il payload in una stringa JSON
    payload[length] = '\0';
    String jsonString = String((char*)payload);

    // Parsing del JSON
    DynamicJsonDocument jsonDoc(200);
    DeserializationError error = deserializeJson(jsonDoc, jsonString);

    // Controlla se il parsing ha avuto successo
    if (error) {
      Serial.print("Parsing JSON failed: ");
      Serial.println(error.c_str());
      return;
    }

    // Estrai i valori dal JSON
    const char* tipoIrrigazione = jsonDoc["tipoIrrigazione"].as<const char*>();
    const char* idAttuatore = jsonDoc["idAttuatore"].as<const char*>();
    float quantita = jsonDoc["quantita"];

    // Fai qualcosa con i valori estratti
    Serial.print("Tipo Irrigazione: ");
    Serial.println(tipoIrrigazione);
    Serial.print("ID Attuatore: ");
    Serial.println(idAttuatore);
    Serial.print("Quantità: ");
    Serial.println(quantita);

    // Calcola il ritardo (delay) in base alla quantità (1 ml = 4000 ms)
    int ritardo = quantita * 4000;
    // Controlla il tipo di irrigazione
    if (strcmp(tipoIrrigazione, "alta") == 0) {
      // Tipo di irrigazione "alto": eroga tutta l'acqua subito
      erogaAcqua(idAttuatore, ritardo);
    } else if (strcmp(tipoIrrigazione, "media") == 0) {
      // Tipo di irrigazione "medio": eroga la quantità richiesta con un ritardo tra le erogazioni
      erogaMedio(idAttuatore, ritardo);
    } else if (strcmp(tipoIrrigazione, "poca") == 0) {
      // Tipo di irrigazione "basso": eroga la quantità richiesta con un ritardo ancora maggiore tra le erogazioni
      erogaBasso(idAttuatore, ritardo);
    }
  } else if (strcmp(topic, "/scheduling") == 0) {
    // Gestisci i messaggi sul topic /scheduling
    // Esegui il parsing del JSON per estrarre i dati aggiuntivi, come l'orario di irrigazione
    payload[length] = '\0';
    String jsonString = String((char*)payload);

    // Parsing del JSON
    DynamicJsonDocument jsonDoc(200);
    DeserializationError error = deserializeJson(jsonDoc, jsonString);

    // Controlla se il parsing ha avuto successo
    if (error) {
      Serial.print("Parsing JSON failed: ");
      Serial.println(error.c_str());
      return;
    }

  // Estrai i valori dal JSON
    const char* tipoIrrigazione = jsonDoc["tipoIrrigazione"].as<const char*>();
    const char* idAttuatore = jsonDoc["idAttuatore"].as<const char*>();
    float quantita = jsonDoc["quantita"];
    // Estrai l'orario di irrigazione come stringa nel formato "ora:minuto"
    const char* orarioIrrigazioneString = jsonDoc["orarioIrrigazione"].as<const char*>();

    // Dichiarazione delle variabili per l'orario
    int ora, minuto;

    // Esegui il parsing dell'orario in ore e minuti
    if (sscanf(orarioIrrigazioneString, "%d:%d", &ora, &minuto) != 2) {
      // Errore nel parsing dell'orario
      Serial.println("Errore nel parsing dell'orario di irrigazione.");
      return;
    }

    // Fai qualcosa con i valori estratti
    Serial.print("Tipo Irrigazione: ");
    Serial.println(tipoIrrigazione);
    Serial.print("ID Attuatore: ");
    Serial.println(idAttuatore);
    Serial.print("Quantità: ");
    Serial.println(quantita);
    Serial.print("Orario di Irrigazione (ora:minuto): ");
    Serial.println(orarioIrrigazioneString);

    // Ora e minuto sono ora disponibili come variabili separate
    Serial.print("Ora: ");
    Serial.println(ora);
    Serial.print("Minuto: ");
    Serial.println(minuto);
    // Esegui le operazioni necessarie basate sull'orario di irrigazione
  }
}


void erogaAcqua(const char* idAttuatore, float quantita) {
  int ritardo = quantita * 4000; // Calcola il ritardo in base alla quantità (1 ml = 4000 ms)

  if (strcmp(idAttuatore, "pompa1a") == 0) {
    digitalWrite(relayD2Pin, HIGH);
    delay(ritardo);
    digitalWrite(relayD2Pin, LOW);
  } else if (strcmp(idAttuatore, "pompa2a") == 0) {
    digitalWrite(relayD3Pin, HIGH);
    delay(ritardo);
    digitalWrite(relayD3Pin, LOW);
  } else if (strcmp(idAttuatore, "pompa1b") == 0) {
    digitalWrite(relayD4Pin, HIGH);
    delay(ritardo);
    digitalWrite(relayD4Pin, LOW);
  } else if (strcmp(idAttuatore, "pompa2b") == 0) {
    digitalWrite(relayD5Pin, HIGH);
    delay(ritardo);
    digitalWrite(relayD5Pin, LOW);
  }
}

void erogaMedio(const char* idAttuatore, float quantita) {
  int ritardo = quantita * 4000; // Calcola il ritardo in base alla quantità (1 ml = 4000 ms)
  int intervallo = 2000; // Intervallo tra le erogazioni (esempio)

  while (quantita > 0) {
    if (quantita >= 5.0) {
      // Eroga 5 ml alla volta
      if (strcmp(idAttuatore, "pompa1a") == 0) {
        digitalWrite(relayD2Pin, HIGH);
        delay(20000); // Tempo approssimativo per erogare 5 ml
        digitalWrite(relayD2Pin, LOW);
      } else if (strcmp(idAttuatore, "pompa2a") == 0) {
        digitalWrite(relayD3Pin, HIGH);
        delay(20000); // Tempo approssimativo per erogare 5 ml
        digitalWrite(relayD3Pin, LOW);
      } else if (strcmp(idAttuatore, "pompa1b") == 0) {
        digitalWrite(relayD4Pin, HIGH);
        delay(20000); // Tempo approssimativo per erogare 5 ml
        digitalWrite(relayD4Pin, LOW);
      } else if (strcmp(idAttuatore, "pompa2b") == 0) {
        digitalWrite(relayD5Pin, HIGH);
        delay(20000); // Tempo approssimativo per erogare 5 ml
        digitalWrite(relayD5Pin, LOW);
      }

      quantita -= 5.0; // Sottrai 5 ml dalla quantità totale
    } else {
      // Eroga la quantità rimanente
      if (strcmp(idAttuatore, "pompa1a") == 0) {
        digitalWrite(relayD2Pin, HIGH);
        delay(quantita * 4000); // Tempo per erogare la quantità rimanente
        digitalWrite(relayD2Pin, LOW);
      } else if (strcmp(idAttuatore, "pompa2a") == 0) {
        digitalWrite(relayD3Pin, HIGH);
        delay(quantita * 4000); // Tempo per erogare la quantità rimanente
        digitalWrite(relayD3Pin, LOW);
      } else if (strcmp(idAttuatore, "pompa1b") == 0) {
        digitalWrite(relayD4Pin, HIGH);
        delay(quantita * 4000); // Tempo per erogare la quantità rimanente
        digitalWrite(relayD4Pin, LOW);
      } else if (strcmp(idAttuatore, "pompa2b") == 0) {
        digitalWrite(relayD5Pin, HIGH);
        delay(quantita * 4000); // Tempo per erogare la quantità rimanente
        digitalWrite(relayD5Pin, LOW);
      }
      quantita = 0; // La quantità è stata completamente erogata
    }

    // Aggiungi un intervallo tra le erogazioni successive
    delay(intervallo);
  }
}

void erogaBasso(const char* idAttuatore, float quantita) {
  int ritardo = quantita * 4000; // Calcola il ritardo in base alla quantità (1 ml = 4000 ms)
  int intervallo = 4000; // Intervallo più lungo tra le erogazioni (esempio)

  while (quantita > 0) {
    if (quantita >= 5.0) {
      // Eroga 5 ml alla volta
      if (strcmp(idAttuatore, "pompa1a") == 0) {
        digitalWrite(relayD2Pin, HIGH);
        delay(20000); // Tempo approssimativo per erogare 5 ml
        digitalWrite(relayD2Pin, LOW);
      } else if (strcmp(idAttuatore, "pompa2a") == 0) {
        digitalWrite(relayD3Pin, HIGH);
        delay(20000); // Tempo approssimativo per erogare 5 ml
        digitalWrite(relayD3Pin, LOW);
      } else if (strcmp(idAttuatore, "pompa1b") == 0) {
        digitalWrite(relayD4Pin, HIGH);
        delay(20000); // Tempo approssimativo per erogare 5 ml
        digitalWrite(relayD4Pin, LOW);
      } else if (strcmp(idAttuatore, "pompa2b") == 0) {
        digitalWrite(relayD5Pin, HIGH);
        delay(20000); // Tempo approssimativo per erogare 5 ml
        digitalWrite(relayD5Pin, LOW);
      }

      quantita -= 5.0; // Sottrai 5 ml dalla quantità totale
    } else {
      // Eroga la quantità rimanente
      if (strcmp(idAttuatore, "pompa1a") == 0) {
        digitalWrite(relayD2Pin, HIGH);
        delay(quantita * 4000); // Tempo per erogare la quantità rimanente
        digitalWrite(relayD2Pin, LOW);
      } else if (strcmp(idAttuatore, "pompa2a") == 0) {
        digitalWrite(relayD3Pin, HIGH);
        delay(quantita * 4000); // Tempo per erogare la quantità rimanente
        digitalWrite(relayD3Pin, LOW);
      } else if (strcmp(idAttuatore, "pompa1b") == 0) {
        digitalWrite(relayD4Pin, HIGH);
        delay(quantita * 4000); // Tempo per erogare la quantità rimanente
        digitalWrite(relayD4Pin, LOW);
      } else if (strcmp(idAttuatore, "pompa2b") == 0) {
        digitalWrite(relayD5Pin, HIGH);
        delay(quantita * 4000); // Tempo per erogare la quantità rimanente
        digitalWrite(relayD5Pin, LOW);
      }
      quantita = 0; // La quantità è stata completamente erogata
    }

    // Aggiungi un intervallo più lungo tra le erogazioni successive
    delay(intervallo);
  }
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
// Funzione per eseguire il codice di irrigazione in base al programma programmato
void eseguiCodiceIrrigazione(ProgrammaIrrigazione programma) {
  Serial.println("Esecuzione del programma di irrigazione:");

  // Puoi utilizzare i dati del programma (programma.tipoIrrigazione, programma.idAttuatore, programma.quantita) per eseguire l'irrigazione qui.
  // ...

  // Ora devi eseguire il codice di erogazione basato sui dati del programma
  if (strcmp(programma.tipoIrrigazione, "alta") == 0) {
    // Tipo di irrigazione "alto": eroga tutta l'acqua subito
    erogaAcqua(programma.idAttuatore, programma.quantita);
  } else if (strcmp(programma.tipoIrrigazione, "media") == 0) {
    // Tipo di irrigazione "medio": eroga la quantità richiesta con un ritardo tra le erogazioni
    erogaMedio(programma.idAttuatore, programma.quantita);
  } else if (strcmp(programma.tipoIrrigazione, "basso") == 0) {
    // Tipo di irrigazione "basso": eroga la quantità richiesta con un ritardo ancora maggiore tra le erogazioni
    erogaBasso(programma.idAttuatore, programma.quantita);
  }
}
void announceAttuatori(const char* tipoAttuatore, const char* nomeAttuatore, const char* idCampo) {
  // Annuncio del sensore nel topic /announce
  StaticJsonDocument<200> jsonDoc;
  jsonDoc["tipoAttuatore"] = tipoAttuatore;
  jsonDoc["nome"] = nomeAttuatore;
  jsonDoc["idCampo"] = idCampo;
  String jsonStr;
  serializeJson(jsonDoc, jsonStr);
  client.publish("/announce", jsonStr.c_str());
}

void announceAttuatoris() {
  // Annuncio dei sensori al topic /announce
  announceAttuatori("pompa", "pompa1a", "6t39cpuv1lke");
  announceAttuatori("pompa", "pompa2a", "a7kb5rqp9jes6");
  announceAttuatori("pompa", "pompa1b", "w2oih8lb3atrc");
  announceAttuatori("pompa", "pompa2b", "1fu6nm9eyadq3");
}


