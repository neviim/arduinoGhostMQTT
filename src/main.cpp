// mosquitto_sub -t home/sensores/arduino/#
// por: neviim jads - 2019
//

#include <SPI.h>
#include <DHT.h>
#include <DHT_U.h>
#include <Arduino.h>
#include <Ethernet.h>
#include <PubSubClient.h>
#include <Adafruit_Sensor.h>

//.. define pinagem dos sensores
#define DHTTYPE DHT22  // Sensor DHT 22  (AM2302)
#define DHTPIN 7

//.. Definicoes do sensor : pino, tipo
DHT_Unified dht(DHTPIN, DHTTYPE);

uint32_t delayMS;

//.. Define variaveis sensores
const int relayPin = 8;  // pino para o led
int umidadeStatus = 1;
bool dhtVerificado = false;

unsigned long previousMillisDHT = 0;
unsigned long intervalDHT = 20000;   // nota: 60000 = 1 minuto

//.. Update these with values suitable for your network.
byte MAC_ADDRESS[] = { 0xDE, 0xED, 0xBA, 0xFE, 0xFF, 0xFE };
byte MQTT_SERVER[] = { 10, 1, 1, 5 };
byte IP_ADDRESS[] = { 172, 16, 0, 100 };

//.. mqtt configuration
const char* clientName = "neviim.arduino_uno.escritorio";
const char* mqttServer = "10.1.1.5";   // Server Zabbix
const int mqttPort = 1883;
const String topicTemperatura = "home/sensores/arduino/temperatura/escritorio";
const String topicUmidadeValor = "home/sensores/arduino/umidade/escritorio/valor";
const String topicUmidadeStatus = "home/sensores/arduino/umidade/escritorio/status";

EthernetClient ethClient;
PubSubClient client(ethClient);

//.. Funções
void dhcpConfig();              // Configura ethernet DHCP
void dhtLeitura();              // Le os dados do DHT22
void dhtStatus();               // Mostra status do DHT22 
void dhtTimeLeitura();          // ler dht em tempo determinao
void mantemConexoes();          // Garante aticas as conexoes com MQTT Broker e Wifi
void mqttReConnect();           // Faz conexão com Broker MQTT
void mqttEmit(String topic, String value);
void callback(char* topic, byte* payload, unsigned int length);

//.. Descreve as funções.
void dhcpConfig() {     // modulo de inicializacao do DHCP na rede local.
  Serial.println("Iniciando coneccão a internet...");

  if (Ethernet.begin(MAC_ADDRESS) == 0)  // Inicializa em DHCP Modo
  {
    Serial.println("Falha para configurar usando Ethernet DHCP, utilizando modo Statico");
    // Se o DHCP Mode falhar, inicializa o ip Statico
    Ethernet.begin(MAC_ADDRESS, IP_ADDRESS);
  }

  Serial.print("Meu IP address: ");
  for (byte thisByte = 0; thisByte < 4; thisByte++)
  {
    // print o valor de each byte do IP address:
    Serial.print(Ethernet.localIP()[thisByte], DEC);
    Serial.print("."); 
  }  

  Serial.println();
  delay(1000);
  Serial.println("conectado...");
}

void dhtStatus() {      // Print temperature e humidade sensor details.
  Serial.println(F("DHTxx Unified Sensor Example"));
  // Print temperatura detalhe do sensor.
  sensor_t sensor;
  dht.temperature().getSensor(&sensor);
  Serial.println(F("------------------------------------"));
  Serial.println(F("Temperature Sensor"));
  Serial.print  (F("Sensor Type: ")); Serial.println(sensor.name);
  Serial.print  (F("Driver Ver:  ")); Serial.println(sensor.version);
  Serial.print  (F("Unique ID:   ")); Serial.println(sensor.sensor_id);
  Serial.print  (F("Max Value:   ")); Serial.print(sensor.max_value);  Serial.println(F("°C"));
  Serial.print  (F("Min Value:   ")); Serial.print(sensor.min_value);  Serial.println(F("°C"));
  Serial.print  (F("Resolution:  ")); Serial.print(sensor.resolution); Serial.println(F("°C"));
  Serial.println(F("------------------------------------"));
  // Print umidade detalhe do sensor.
  dht.humidity().getSensor(&sensor);
  Serial.println(F("Humidity Sensor"));
  Serial.print  (F("Sensor Type: ")); Serial.println(sensor.name);
  Serial.print  (F("Driver Ver:  ")); Serial.println(sensor.version);
  Serial.print  (F("Unique ID:   ")); Serial.println(sensor.sensor_id);
  Serial.print  (F("Max Value:   ")); Serial.print(sensor.max_value);  Serial.println(F("%"));
  Serial.print  (F("Min Value:   ")); Serial.print(sensor.min_value);  Serial.println(F("%"));
  Serial.print  (F("Resolution:  ")); Serial.print(sensor.resolution); Serial.println(F("%"));
  Serial.println(F("------------------------------------"));
  // Set delay between sensor readings based on sensor details.
  delayMS = sensor.min_delay / 1000;
}

void dhtLeitura() {     // sensor de temperatura DHT22
  // Delay between measurements.
  delay(delayMS);
  // Get temperature event and print its value.
  sensors_event_t event;

  dht.temperature().getEvent(&event);
  if (isnan(event.temperature)) {
    Serial.println(F("Error lendo temperatura!"));
  }
  else {
    mqttEmit(topicTemperatura, (String) event.temperature);
    Serial.print(F("Temperatura: "));
    Serial.print(event.temperature);
    Serial.println(F("°C"));
  }

  // Get humidity event and print its value.
  dht.humidity().getEvent(&event);
  if (isnan(event.relative_humidity)) {
    Serial.println(F("Error lendo umidade!"));
  }
  else {
    mqttEmit(topicUmidadeValor, (String) event.relative_humidity);
    Serial.print(F("Umidade: "));
    Serial.print(event.relative_humidity);
    Serial.println(F("%"));
    
    // Umidade acima de 70%
    if (event.relative_humidity <= 55) {
      umidadeStatus = 0;
    } else {
      umidadeStatus = 1;
    }
    mqttEmit(topicUmidadeStatus, umidadeStatus ? "1" : "0");
  }
}

void dhtTimeLeitura() { // Chama leitura de tempo em tempo determinado
  // Rotina para calcular um intervalo de 1 terco de um minuto
  unsigned long currentMillis = millis();

  // tempo para ler semsor de temperatura DHT = 60000 = 1 Minutos
  if ((unsigned long)(currentMillis - previousMillisDHT) >= intervalDHT) {
    dhtLeitura();
    dhtVerificado = true;
    previousMillisDHT = currentMillis; // salva corrente time
  }
}

void mantemConexoes() { // Verifica conectividade da rede e servico MQTT
  if (!client.connected()) {
    mqttReConnect();
  }
}

void mqttReConnect() {  // Reconecta MQTT
  while (!client.connected()) {
    Serial.print("Altenticando MQTT, conectando...");
    if (client.connect(clientName)) {
      Serial.println("conectado");
      client.subscribe(topicTemperatura.c_str());
      client.subscribe(topicUmidadeValor.c_str());
    } else {
      Serial.print("Falha, rc=");
      Serial.print(client.state());
      Serial.println(" retentando em 5 segundos");
      delay(5000);
    }
  }
}

void mqttEmit(String topic, String value) {
  if (client.publish((char*) topic.c_str(), (char*) value.c_str())) {
    //Serial.print("Publish ok (topic: ");
    //Serial.print(topic);
    //Serial.print(", value: ");
    //Serial.print(value);
    //Serial.println(")");
  } else {
    Serial.println("Publish falhou");
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] payload: ");
  String data;
  for (size_t i = 0; i < length; i++) {
    data += (char)payload[i];
  }  
  Serial.println("");
}

void setup() {
  Serial.begin(9600);

  dhcpConfig(); // Inicializando ip por DHCP
  dht.begin();  // Iniclaiza o sensor DHT
  dhtStatus();  // Status temperatura

  // pino led de status Umidade
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW);

  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);

  delay(1500);
}

void loop() {
  // Captura de dados a ser enviados
  dhtTimeLeitura(); // Le a cada X segundos o DHT22, 20000 = 1 terço de minuto.

  // esta conectado?
  mantemConexoes();

  client.loop();

  if (dhtVerificado) { // DHT22 foi atualizado.
    // status led de umidade
    if (umidadeStatus == 1) {
      digitalWrite(relayPin, HIGH); // turn on  LED
    } else {
      digitalWrite(relayPin, LOW);  // turn off LED 
    }
    
    
    // só entrara neste if quando DHT22 for releito
    dhtVerificado = false;
  }
}