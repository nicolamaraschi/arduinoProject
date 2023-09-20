// Pin del sensore di umidità
int sensorPin1 = A0;
int sensorPin2 = A1;

void setup() {
  // Inizializza il monitor seriale
  Serial.begin(9600);
  
  Serial2.begin(4800);
  // Inizializza la connessione seriale con ESP8266 sulla porta seriale hardware 1 (TX1, RX1)
  Serial1.begin(9600);

  // Configura e connetti l'ESP8266 qui
}

void loop() {
  // Leggi il valore di umidità dal sensore
  int sensorValue1 = analogRead(sensorPin1);
  int sensorValue2 = analogRead(sensorPin2);

  // Calcola l'umidità in base ai tuoi dati
  // Supponiamo che il sensore restituisca valori tra 0 e 1023
  // e che tu abbia una calibrazione appropriata per ottenere l'umidità reale.
  float humidity1 = map(sensorValue1, 0, 1023, 0, 100); // Esempio di mappatura
  float humidity2 = map(sensorValue2, 0, 1023, 0, 100); // Esempio di mappatura

  // Invia il valore all'ESP8266 tramite seriale
  Serial1.print(humidity1);
  Serial2.print(humidity2);

 Serial.println("humi1: ");
  Serial.println(humidity1);
   Serial.println("humi2: ");
  Serial.println(humidity2);
  delay(1000); // Attendi un secondo prima di eseguire una nuova lettura
}
