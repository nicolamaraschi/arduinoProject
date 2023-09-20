#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <EEPROM.h>
#include <WiFiUdp.h>
#include <ArduinoUnit.h>

const int utcOffsetInSeconds = 3600; // Offset orario (in secondi) dalla UTC (ad esempio, 1 ora)
WiFiUDP ntpUDP;

NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);

//per il testing poi da CANCELLARE
const char* actionMessage = "{\"tipoIrrigazione\":\"alta\",\"idAttuatore\":\"pompa1a\",\"quantita\":5.0}";
const char* schedulingMessage = "{\"tipoIrrigazione\":\"media\",\"idAttuatore\":\"pompa2a\",\"quantita\":7.0,\"orarioIrrigazione\":\"10:30\"}";

// Impostazioni server NTP
const char* ntpServerName = "pool.ntp.org";

const char* ssid = "FASTWEB-PXPSTE";
const char* password = "43LY9LDLPH";
const char* mqttServer = "pissir.colnet.rocks";
const int mqttPort = 1883;
const char* actionTopic = "/action";
bool announced = false;
const int MAX_TOLLERANZA_MINUTI = 5; // Imposta la tua tolleranza in minuti


WiFiClient espClient;
PubSubClient client(espClient);


const int relayD2Pin = 4;
const int relayD3Pin = 0;
const int relayD4Pin = 2;
const int relayD5Pin = 14;


struct ProgrammaIrrigazione;
void erogaAcqua(const char* idAttuatore, float quantita);
void erogaMedio(const char* idAttuatore, float quantita);
void erogaBasso(const char* idAttuatore, float quantita);
void callback(char* topic, byte* payload, unsigned int length);
bool programmaEsiste(const char* idAttuatore);
void creaProgramma(const char* tipoIrrigazione, const char* idAttuatore, float quantita, int ora, int minuto);
void sovrascriviProgramma(const char* tipoIrrigazione, const char* idAttuatore, float quantita, int ora, int minuto);
void rimuoviProgramma(const char* idAttuatore);
void executeIrrigations();
void addToIrrigationQueue(const char* idAttuatore, const char* tipoIrrigazione, int ritardo);
void announceAttuatori(const char* tipoAttuatore, const char* nomeAttuatore, const char* idCampo);
void announceAttuatoris();
void reconnect();
void publishQueueStatus();

const int maxQueueSize = 10; // Puoi regolare la dimensione massima della coda a seconda delle tue esigenze
String irrigationQueue[maxQueueSize];
int queueFront = 0;
int queueRear = 0;

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
  client.setCallback(callback_test);
  //client.setCallback(myMqttCallback);


  //gestione rele 
  pinMode(relayD2Pin, OUTPUT);
  pinMode(relayD3Pin, OUTPUT);
  pinMode(relayD4Pin, OUTPUT);
  pinMode(relayD5Pin, OUTPUT);

  digitalWrite(relayD2Pin, LOW);
  digitalWrite(relayD3Pin, LOW);
  digitalWrite(relayD4Pin, LOW);
  digitalWrite(relayD5Pin, LOW);


  // Inizializza la libreria NTPClient
  timeClient.begin();
  timeClient.setTimeOffset(utcOffsetInSeconds);


  // Recupera l'ora corrente dal server NTP
  while (!timeClient.update()) {
    timeClient.forceUpdate();
  }

  // Connessione al server MQTT
  if (client.connect("ESP8266Client")) {
    Serial.println("Connesso al server MQTT");
    // Sottoscrivi il client ai topic desiderati
    client.subscribe("/action"); 
    client.subscribe("/scheduling"); 
  } else {
  Serial.println("Connessione al server MQTT fallita");
  }
}

void loop() {

  if (!client.connected()) {
    reconnect();
  }

  client.loop();

  // Esegui announceAttuatoris() solo una volta
  if (!announced) {
    announceAttuatoris();
    announced = true; // Imposta la variabile a true per evitare esecuzioni successive
  }

  // Recupera l'ora corrente dal server NTP
  timeClient.update();
  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();

  // Scorrere la coda delle irrigazioni e eseguire i comandi se presenti
  executeIrrigations();

   // Confronta l'orario corrente con i programmi di irrigazione salvati in EEPROM
  for (int i = 0; i < NUMERO_PROGRAMMI_IRRIGAZIONE; i++) {
    ProgrammaIrrigazione programma;
    EEPROM.get(EEPROM_ADDRESS + i * EEPROM_SIZE, programma);

    // Calcola la differenza tra l'orario del programma e l'orario corrente
    int hourDifference = abs(currentHour - programma.ora);
    int minuteDifference = abs(currentMinute - programma.minuto);

    // Verifica se l'orario del programma è vicino all'orario corrente entro la tolleranza
    if (hourDifference * 60 + minuteDifference <= MAX_TOLLERANZA_MINUTI) {
      // Calcola il ritardo (delay) in base alla quantità (1 ml = 4000 ms)
      int ritardo = programma.quantita * 4000;
      // Esegui il codice di irrigazione per questo programma
      addToIrrigationQueue(programma.idAttuatore, programma.tipoIrrigazione, ritardo);
    }
  }
 
}

void callback_test(char* topic, byte* payload, unsigned int length) {
  Serial.println("Message arrived in topic: " + String(topic));

  Serial.print("Payload: ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

void myMqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.println("Messaggio ricevuto sul topic: " + String(topic));

  // Verifica se il messaggio è sul topic di interesse (actionTopic)
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

    // Ora hai i dati del messaggio JSON e puoi eseguire le operazioni necessarie
    // in base ai valori estratti.
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

    // Aggiungi il comando di irrigazione alla coda
    addToIrrigationQueue(idAttuatore, tipoIrrigazione, ritardo);
  } 
  else if (strcmp(topic, "/scheduling") == 0) {
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
    String orarioIrrigazioneString = jsonDoc["orarioIrrigazione"].as<String>(); // Converti in stringa

    // Dichiarazione delle variabili per l'orario
    int ora, minuto;

    // Esegui il parsing dell'orario in ore e minuti
    if (sscanf(orarioIrrigazioneString.c_str(), "%d:%d", &ora, &minuto) != 2) {
      // Errore nel parsing dell'orario
      Serial.println("Errore nel parsing dell'orario di irrigazione.");
      return;
    }

    // Verifica se esiste già un programma per l'attuatore specificato
    if (programmaEsiste(idAttuatore)) {
      // Sovrascrivi il programma esistente con quello nuovo
      sovrascriviProgramma(tipoIrrigazione, idAttuatore, quantita, ora, minuto);
    } else {
      // Se non esiste un programma per l'attuatore, crea un nuovo programma
      creaProgramma(tipoIrrigazione, idAttuatore, quantita, ora, minuto);
    }

    // Controlla se orarioIrrigazione è uguale a "null" e rimuovi il programma se lo è
    if (orarioIrrigazioneString == "null") {
      rimuoviProgramma(idAttuatore);
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
  }
}

// Funzione per aggiungere un comando di irrigazione alla coda
void addToIrrigationQueue(const char* idAttuatore, const char* tipoIrrigazione, int ritardo) {
  int nextQueueRear = (queueRear + 1) % maxQueueSize;
  
  // Verifica se la coda è piena
  if (nextQueueRear == queueFront) {
    Serial.println("Queue is full. Cannot add more irrigations.");
    return;
  }

  // Aggiungi il comando alla coda
  irrigationQueue[queueRear] = String(idAttuatore) + "," + String(tipoIrrigazione) + "," + String(ritardo);
  queueRear = nextQueueRear;
}

// Funzione per eseguire le irrigazioni dalla coda
void executeIrrigations() {
  // Verifica se la coda delle irrigazioni non è vuota
  if (queueFront != queueRear) {
    // Estrai il prossimo comando dalla coda
    String command = irrigationQueue[queueFront];
    
    // Parsa il comando per ottenere idAttuatore, tipoIrrigazione e ritardo
    int comma1 = command.indexOf(",");
    int comma2 = command.indexOf(",", comma1 + 1);
    String idAttuatore = command.substring(0, comma1);
    String tipoIrrigazione = command.substring(comma1 + 1, comma2);
    int ritardo = command.substring(comma2 + 1).toInt();

    // Esegui l'irrigazione in base al tipo
    if (tipoIrrigazione == "alta") {
      erogaAcquaNew(idAttuatore.c_str(), ritardo,1);
    } else if (tipoIrrigazione == "media") {
      erogaAcquaNew(idAttuatore.c_str(), ritardo,2);
    } else if (tipoIrrigazione == "poca") {
      erogaAcquaNew(idAttuatore.c_str(), ritardo,4);
    }

    // Rimuovi il comando dalla coda
    queueFront = (queueFront + 1) % maxQueueSize;
  }
}

void erogaAcquaNew(const char* idAttuatore, int ritardo, int numeroIrrigazioni){
  
  int conteggio = 1;

  while(conteggio <= numeroIrrigazioni){

    if (strcmp(idAttuatore, "6t39cpuv1lke") == 0) {
      digitalWrite(relayD2Pin, HIGH);
      delay(ritardo/numeroIrrigazioni);
      digitalWrite(relayD2Pin, LOW);
    } else if (strcmp(idAttuatore, "a7kb5rqp9jes6") == 0) {
      digitalWrite(relayD3Pin, HIGH);
      delay(ritardo/numeroIrrigazioni);
      digitalWrite(relayD3Pin, LOW);
    } else if (strcmp(idAttuatore, "w2oih8lb3atrc") == 0) {
      digitalWrite(relayD4Pin, HIGH);
      delay(ritardo/numeroIrrigazioni);
      digitalWrite(relayD4Pin, LOW);
    } else if (strcmp(idAttuatore, "pompa2b") == 0) {
      digitalWrite(relayD5Pin, HIGH);
      delay(ritardo/numeroIrrigazioni);
      digitalWrite(relayD5Pin, LOW);
    }

    delay(5000);
    conteggio++;
  }
  
}


bool programmaEsiste(const char* idAttuatore) {
  for (int i = 0; i < NUMERO_PROGRAMMI_IRRIGAZIONE; i++) {
    ProgrammaIrrigazione programma;
    EEPROM.get(EEPROM_ADDRESS + i * EEPROM_SIZE, programma);

    if (strcmp(programma.idAttuatore, idAttuatore) == 0) {
      return true;
    }
  }

  return false;
}

// Aggiungi questa funzione per creare un nuovo programma
void creaProgramma(const char* tipoIrrigazione, const char* idAttuatore, float quantita, int ora, int minuto) {
  int slotVuoto = -1;
  for (int i = 0; i < NUMERO_PROGRAMMI_IRRIGAZIONE; i++) {
    ProgrammaIrrigazione programma;
    EEPROM.get(EEPROM_ADDRESS + i * EEPROM_SIZE, programma);

    if (strlen(programma.idAttuatore) == 0) {
      // Questo slot è vuoto, possiamo usarlo
      slotVuoto = i;
      break;
    }
  }

  if (slotVuoto != -1) {
    // Trovato uno slot vuoto, crea un nuovo programma
    ProgrammaIrrigazione nuovoProgramma;
    strcpy(nuovoProgramma.tipoIrrigazione, tipoIrrigazione);
    strcpy(nuovoProgramma.idAttuatore, idAttuatore);
    nuovoProgramma.quantita = quantita;
    nuovoProgramma.ora = ora;
    nuovoProgramma.minuto = minuto;

    // Scrivi il nuovo programma nella EEPROM
    EEPROM.put(EEPROM_ADDRESS + slotVuoto * EEPROM_SIZE, nuovoProgramma);
    EEPROM.commit();
    Serial.println("Nuovo programma creato e memorizzato nella EEPROM.");
  } else {
    Serial.println("Nessuno slot vuoto trovato nella EEPROM. Impossibile memorizzare il nuovo programma.");
  }
}

// Aggiungi questa funzione per sovrascrivere un programma esistente
void sovrascriviProgramma(const char* tipoIrrigazione, const char* idAttuatore, float quantita, int ora, int minuto) {
  for (int i = 0; i < NUMERO_PROGRAMMI_IRRIGAZIONE; i++) {
    ProgrammaIrrigazione programma;
    EEPROM.get(EEPROM_ADDRESS + i * EEPROM_SIZE, programma);

    if (strcmp(programma.idAttuatore, idAttuatore) == 0) {
      // Sovrascrivi il programma con i nuovi dati
      strcpy(programma.tipoIrrigazione, tipoIrrigazione);
      programma.quantita = quantita;
      programma.ora = ora;
      programma.minuto = minuto;

      // Scrivi il programma sovrascritto nella EEPROM
      EEPROM.put(EEPROM_ADDRESS + i * EEPROM_SIZE, programma);
      EEPROM.commit();
      Serial.println("Programma per attuatore sovrascritto con successo.");
      return;
    }
  }

  Serial.println("Programma per attuatore non trovato. Impossibile sovrascrivere.");
}

// Aggiungi questa funzione per rimuovere un programma esistente
void rimuoviProgramma(const char* idAttuatore) {
  for (int i = 0; i < NUMERO_PROGRAMMI_IRRIGAZIONE; i++) {
    ProgrammaIrrigazione programma;
    EEPROM.get(EEPROM_ADDRESS + i * EEPROM_SIZE, programma);

    if (strcmp(programma.idAttuatore, idAttuatore) == 0) {
      // Rimuovi il programma cancellando i dati dalla memoria EEPROM
      memset(&programma, 0, sizeof(ProgrammaIrrigazione));
      EEPROM.put(EEPROM_ADDRESS + i * EEPROM_SIZE, programma);
      EEPROM.commit();
      Serial.println("Programma per attuatore rimosso con successo.");
      return;
    }
  }

  Serial.println("Programma per attuatore non trovato. Impossibile rimuovere.");
}


void testProgrammiIrrigazione() {
  // Test di programmaEsiste
  const char* idAttuatore1 = "Attuatore1";
  const char* idAttuatore2 = "Attuatore2";
  
  // Inizializza la EEPROM con dati di test
  ProgrammaIrrigazione programma1;
  strcpy(programma1.idAttuatore, idAttuatore1);
  EEPROM.put(EEPROM_ADDRESS, programma1);

  ProgrammaIrrigazione programma2;
  strcpy(programma2.idAttuatore, idAttuatore2);
  EEPROM.put(EEPROM_ADDRESS + EEPROM_SIZE, programma2);

  // Verifica se i programmi esistono
  bool result1 = programmaEsiste(idAttuatore1);
  bool result2 = programmaEsiste(idAttuatore2);

  Serial.println("Test programmaEsiste:");
  Serial.print("Programma 1 esiste: ");
  if (result1) Serial.println("test passato");
  else Serial.println("test fallito");
  
  Serial.print("Programma 2 esiste: ");
  if (result2) Serial.println("test passato");
  else Serial.println("test fallito");
  
  // Test di creaProgramma
  const char* tipoIrrigazione1 = "Tipo1";
  float quantita1 = 1.5;
  int ora1 = 10;
  int minuto1 = 30;

  Serial.println("Test creaProgramma:");
  creaProgramma(tipoIrrigazione1, idAttuatore1, quantita1, ora1, minuto1);
  bool result3 = programmaEsiste(idAttuatore1);

  Serial.print("Creazione Programma 1: ");
  if (result3) Serial.println("test passato");
  else Serial.println("test fallito");
  
  // Test di sovrascriviProgramma
  const char* tipoIrrigazione2 = "Tipo2";
  float quantita2 = 2.0;
  int ora2 = 15;
  int minuto2 = 45;

  Serial.println("Test sovrascriviProgramma:");
  sovrascriviProgramma(tipoIrrigazione2, idAttuatore1, quantita2, ora2, minuto2);

  ProgrammaIrrigazione programmaLetto;
  EEPROM.get(EEPROM_ADDRESS, programmaLetto);

  bool result4 = (strcmp(programmaLetto.tipoIrrigazione, tipoIrrigazione2) == 0) &&
                 (programmaLetto.quantita == quantita2) &&
                 (programmaLetto.ora == ora2) &&
                 (programmaLetto.minuto == minuto2);

  Serial.print("Sovrascrittura Programma 1: ");
  if (result4) Serial.println("test passato");
   else Serial.println("test fallito");
  

  // Test di rimuoviProgramma
  Serial.println("Test rimuoviProgramma:");
  rimuoviProgramma(idAttuatore1);
  bool result5 = programmaEsiste(idAttuatore1);

  Serial.print("Rimozione Programma 1: ");
  if (!result5) Serial.println("test passato");
  else Serial.println("test fallito");

}
void testAllMethods() {
  // Test del metodo callback
  {
    char actionTopic[] = "/action";
    char payload[] = "{\"tipoIrrigazione\":\"alta\",\"idAttuatore\":\"6t39cpuv1lke\",\"quantita\":1.5}";
    int length = strlen(payload);
    callback(actionTopic, (byte*)payload, length);

    // Verifica se il comando di irrigazione è stato aggiunto alla coda correttamente
    assertEqual(queueRear, 1, "Errore nel comando di irrigazione in coda");
  }

  // Test del metodo addToIrrigationQueue
  {
    addToIrrigationQueue("6t39cpuv1lke", "alta", 4000);

    // Verifica se il comando di irrigazione è stato aggiunto alla coda correttamente
    assertEqual(queueRear, 2, "Errore nell'aggiunta del comando di irrigazione alla coda");
  }

  // Test del metodo executeIrrigations
  {
    executeIrrigations();

    // Verifica se l'esecuzione dei comandi di irrigazione ha funzionato correttamente
    assertEqual(queueFront, 1, "Errore nell'esecuzione dei comandi di irrigazione");
  }

  // Test del metodo erogaAcquaNew
  {
    erogaAcquaNew("6t39cpuv1lke", 1000, 2);

    // Verifica se l'erogazione dell'acqua ha funzionato correttamente
    // Includi asserzioni specifiche per il tuo sistema
  }
}


void announceAttuatori(const char* tipoAttuatore, const char* nomeAttuatore) {
  // Annuncio del sensore nel topic /announce
  StaticJsonDocument<200> jsonDoc;
  jsonDoc["tipoAttuatore"] = tipoAttuatore;
  jsonDoc["nome"] = nomeAttuatore;
  String jsonStr;
  serializeJson(jsonDoc, jsonStr);
  client.publish("/announce", jsonStr.c_str());
}

void announceAttuatoris() {
  // Annuncio dei sensori al topic /announce
  announceAttuatori("pompa", "6t39cpuv1lke");
  announceAttuatori("pompa", "a7kb5rqp9jes6");
  announceAttuatori("pompa", "w2oih8lb3atrc");
  announceAttuatori("pompa", "1fu6nm9eyadq3");
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT server...");
    if (client.connect("ESP8266Client")) {
      Serial.println("Connected to MQTT server");
       client.subscribe("/action");
        client.subscribe("/scheduling");
    } else {
      Serial.print("Retrying in 5 seconds...");
      delay(5000);
    }
  }
}

void publishQueueStatus() {
  // Creare un oggetto JSON per rappresentare lo stato della coda
  DynamicJsonDocument doc(1024); // 1024 è la dimensione del buffer JSON, regola secondo necessità

  // Creare un array JSON per la lista dei comandi di irrigazione pendenti
  JsonArray queueArray = doc.createNestedArray("queue");

  // Scorrere la coda delle irrigazioni e aggiungere ogni comando all'array JSON
  int currentIndex = queueFront;
  while (currentIndex != queueRear) {
    String command = irrigationQueue[currentIndex];
    int comma1 = command.indexOf(",");
    int comma2 = command.indexOf(",", comma1 + 1);
    String tipoIrrigazione = command.substring(comma1 + 1, comma2);
    String idAttuatore = command.substring(0, comma1);
    float quantita = command.substring(comma2 + 1).toFloat();

    // Creare un oggetto JSON per rappresentare un singolo comando
    JsonObject commandObj = queueArray.createNestedObject();
    commandObj["tipoIrrigazione"] = tipoIrrigazione;
    commandObj["idAttuatore"] = idAttuatore;
    commandObj["quantita"] = quantita;

    currentIndex = (currentIndex + 1) % maxQueueSize;
  }

  // Serializzare l'oggetto JSON in una stringa
  String jsonString;
  serializeJson(doc, jsonString);

  // Pubblicare la stringa JSON sul topic MQTT /status
  client.publish("/status", jsonString.c_str());
}

