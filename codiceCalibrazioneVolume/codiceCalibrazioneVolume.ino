// Pin 
const int trigPin = 7;
const int echoPin = 8;

// Calibrazione
// con 75 ml tara a 0 volume
//float distances[] = {11, 9,  6.5, 4, 2.6 };
//float volumes[] =   {0, 150.0, 300.0,  450.0, 600};  
float distances[] = {11, 9,  6.5, 4, 3.2, 2.6 };
float volumes[] =   {0, 150.0, 300.0,  450.0, 500, 600};  

// Variabili
float distanceMeasures[10];  
int index = 0;
long duration;
float distance;
float avgDistance;

void setup() {

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  Serial.begin(9600);

}

void loop() {

  // Misura distanza
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  duration = pulseIn(echoPin, HIGH);
  distance = duration * 0.034 / 2;

  // Salva distanza
  distanceMeasures[index] = distance;
  index++;

  // Dopo 10 misure
  if(index >= 10){

    // Calcola media
    avgDistance = 0;
    for(int i=0; i<10; i++){
      avgDistance += distanceMeasures[i];
    }
    avgDistance /= 10.0; 

    // Stima volume medio
    float volume = calculateVolume(avgDistance);
    
    // Stampa volume medio
    Serial.print("Avg Volume: ");
     Serial.println(volume);
      Serial.print("Avg distance: ");
     Serial.print(avgDistance);
   

    // Reset index
    index = 0; 
  }

  delay(1000);

}

float calculateVolume(float distance) {
  if (distance >= distances[0]) {
    return volumes[0];  // Serbatoio vuoto
  } else if (distance <= distances[5]) {
    return volumes[5];  // 650 ml
  } else {
    // Calcola la retta di regressione lineare
    float slope = (volumes[5] - volumes[0]) / (distances[5] - distances[0]);
    float intercept = volumes[0] - slope * distances[0];
    
    // Calcola il volume utilizzando la retta
    return slope * distance + intercept;
  }
}