#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <dht.h>
#include <RH_NRF24.h>
#include <RHReliableDatagram.h>

#define THIS_ADDRESS 2
#define MASTER_ADDRESS 128

#define DHT11_PIN 7
#define ONE_WIRE_BUS 2
#define VALVE_RELAY_PIN 6

dht DHT;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature dallasTemperature(&oneWire);
const int AirValue = 620;
const int WaterValue = 310;
RH_NRF24 driver;
RHReliableDatagram manager(driver, THIS_ADDRESS);

bool isValveOpened = false;

#define PACKET_TYPE_SENSOR_DATA 1
#define PACKET_TYPE_MESSAGE 2
#define PACKET_TYPE_RESPONSE 3
#define PACKET_TYPE_INSTRUCTION 4
#define PACKET_TYPE_RECEIVE_SUCCESS 5

#define INSTRUCTION_SEND_SENSOR_DATA 1
#define INSTRUCTION_OPEN_VALVE 2
#define INSTRUCTION_CLOSE_VALVE 3
#define INSTRUCTION_PREPARE_SENSOR_DATA 4

#define RESPONSE_OPENED_VALVE 5
#define RESPONSE_CLOSED_VALVE 6

unsigned long counter = 0;

void reInitialiseNRF() {
    if (!manager.init())
        Serial.println("init failed");
    driver.setRF(RH_NRF24::DataRate250kbps, RH_NRF24::TransmitPower0dBm);
    driver.setChannel(96);
    manager.setRetries(20);
    manager.setTimeout(200);
}

#define DATA_PACKET_MAX_LENGTH 20

union DataPacket {
    struct {
        long packet_type;
//        unsigned long id;
        union {
            struct {
                long moisture_percent;
                float atmospheric_temperature;
                float humidity;
                float soil_temperature;

                void print() const {
                    Serial.print(F("Sensor Data | "));
                    Serial.print(F("moisture : "));
                    Serial.print(moisture_percent);
                    Serial.print(F(" %\t atm_temp : "));
                    Serial.print(atmospheric_temperature);
                    Serial.print(F(" C\t humidity : "));
                    Serial.print(humidity);
                    Serial.print(F(" \t soil_temp : "));
                    Serial.print(soil_temperature);
                    Serial.println(F(" C"));
                }
            } sensor;

            struct {
                long code;

                void print() {
                    Serial.print(F("Response "));
                    Serial.print(code);
                    Serial.print(F(" | "));
                    switch (code) {
                        case RESPONSE_CLOSED_VALVE:
                            Serial.println(F("RESPONSE_CLOSED_VALVE"));
                            break;
                        case RESPONSE_OPENED_VALVE:
                            Serial.println(F("RESPONSE_OPENED_VALVE"));
                            break;
                    }
                }
            } response;

            struct {
                long code;

                void print() {
                    Serial.print(F("Instruction Received"));
                    Serial.print(code);
                    Serial.print(F(" | "));
                    switch (code) {
                        case INSTRUCTION_OPEN_VALVE:
                            Serial.println(F("INSTRUCTION_OPEN_VALVE"));
                            break;
                        case INSTRUCTION_CLOSE_VALVE:
                            Serial.println(F("INSTRUCTION_CLOSE_VALVE"));
                            break;
                        case INSTRUCTION_SEND_SENSOR_DATA:
                            Serial.println(F("INSTRUCTION_SEND_SENSOR_DATA"));
                            break;
                        case INSTRUCTION_PREPARE_SENSOR_DATA:
                            Serial.println(F("INSTRUCTION_PREPARE_SENSOR_DATA"));
                            break;
                    }
                }
            } instruction;

            char message[DATA_PACKET_MAX_LENGTH - 4];
        } data;

        void print() {
//            Serial.print("#");
//            Serial.print(id);
//            Serial.print(": ");
            switch (packet_type) {
                case PACKET_TYPE_SENSOR_DATA:
                    data.sensor.print();
                    break;
                case PACKET_TYPE_MESSAGE:
                    Serial.print(F("Message >> "));
                    Serial.println(data.message);
                    break;
                case PACKET_TYPE_RESPONSE:
                    data.response.print();
                    break;
                case PACKET_TYPE_INSTRUCTION:
                    data.instruction.print();
                    break;
                case PACKET_TYPE_RECEIVE_SUCCESS:
                    Serial.println("Receive success ack");
            }
        }
    } packet;

    byte bytes[DATA_PACKET_MAX_LENGTH];
};

void readAndConsumeDataPacket();

void sendData(uint8_t *bytes, byte length, uint8_t to) {
    Serial.println("Sending data...");
    for (int i = 0; i < length; i++) {
        Serial.print(bytes[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
    if (!manager.sendtoWait(bytes, length, to)) {
        Serial.println(F("Transmission Failed!!"));
        reInitialiseNRF();
    }
}

DataPacket createDataPacket(int type) {
    DataPacket packet{{type}};
//    packet.packet.id = counter++;
    return packet;
}

//void sendMessage(String text, uint8_t to) {
//    Serial.println(text);
//
//    DataPacket packet = createDataPacket(PACKET_TYPE_MESSAGE);
//    memcpy(packet.packet.data.message, text.begin(), text.length());
//
//    Serial.println("Sending message");
//    packet.packet.print();
//
//    sendData(packet.bytes, sizeof(packet.bytes), to);
//}

void sendResponse(int responseCode, String text, uint8_t to) {
    Serial.println(text);

    DataPacket responsePacket = createDataPacket(PACKET_TYPE_RESPONSE);
    responsePacket.packet.data.response.code = responseCode;
//    memcpy(responsePacket.packet.data.response.text, text.begin(), text.length());

    Serial.println("Sending response...");
    responsePacket.packet.print();

    sendData(responsePacket.bytes, 4 * 1 + 4, to);
}

byte readSoilMoisture() {
    int soilMoistureValue = analogRead(A0);
    int soilMoisturePercent = (int) map(soilMoistureValue, AirValue, WaterValue, 0, 100);
    soilMoisturePercent = constrain(soilMoisturePercent, 0, 100);
    Serial.println(soilMoistureValue);
    Serial.println(soilMoisturePercent);
    return (byte) soilMoisturePercent;
}

int updateDHT() {
    int chk = DHT.read11(DHT11_PIN);
    switch (chk) {
        case DHTLIB_OK:
            Serial.println(F("DHT read success"));
            break;
        case DHTLIB_ERROR_CHECKSUM:
            Serial.println(F("DHT read error checksum"));
            break;
        case DHTLIB_ERROR_TIMEOUT:
            Serial.println(F("DHT read error timeout"));
            break;
        default:
            Serial.println(F("DHT read error"));
    }
//    return chk;
    return DHTLIB_OK;
}

float readSoilTemperature() {
    dallasTemperature.requestTemperatures();
    return dallasTemperature.getTempCByIndex(0);
}

DataPacket sensorPacket = DataPacket{{PACKET_TYPE_SENSOR_DATA}};

void fakePacket() {
    sensorPacket.packet.data.sensor.moisture_percent = 26;
    sensorPacket.packet.data.sensor.atmospheric_temperature = (float) 27.8;
    sensorPacket.packet.data.sensor.humidity = (float) 44.56;
    sensorPacket.packet.data.sensor.soil_temperature = 32.92;
}

void prepareSensorData() {
//    sensorPacket.packet.id = counter++;

//    fakePacket();
//    return;
    updateDHT();
    sensorPacket.packet.data.sensor.moisture_percent = readSoilMoisture();
    sensorPacket.packet.data.sensor.atmospheric_temperature = (float) DHT.temperature;
    sensorPacket.packet.data.sensor.humidity = (float) DHT.humidity;
    sensorPacket.packet.data.sensor.soil_temperature = readSoilTemperature();
}


void sendSensorData(uint8_t to) {
    Serial.println("Sending sensor data...");
    sensorPacket.packet.print();
    sendData(sensorPacket.bytes, 4 * 4 + 4, to);
}


void openValve() {
    if (isValveOpened) return;
    else {
        isValveOpened = true;
        digitalWrite(VALVE_RELAY_PIN, LOW);
    }
}

void closeValve() {
    if (!isValveOpened) return;
    else {
        isValveOpened = false;
        digitalWrite(VALVE_RELAY_PIN, HIGH);
    }
}

void sendReceived(uint8_t to){
    DataPacket receivedSuccess = createDataPacket(PACKET_TYPE_RECEIVE_SUCCESS);
    sendData(receivedSuccess.bytes, 4, to);
}

void readAndConsumeDataPacket() {
    DataPacket dataPacket{};
    uint8_t len = sizeof(dataPacket.bytes);
    uint8_t from;
    if (manager.recvfromAck(dataPacket.bytes, &len, &from)) {
        Serial.print("got request from : 0x");
        Serial.print(from, HEX);
        Serial.print(": ");
        dataPacket.packet.print();

        for (int i = 0; i < len; i++) {
            Serial.print(dataPacket.bytes[i], HEX);
            Serial.print(" ");
        }
        Serial.println();
        switch (dataPacket.packet.packet_type) {
            case PACKET_TYPE_INSTRUCTION:
                switch (dataPacket.packet.data.instruction.code) {
                    case INSTRUCTION_PREPARE_SENSOR_DATA: {
                        prepareSensorData();
                        sendReceived(from);
                        break;
                    }
                    case INSTRUCTION_SEND_SENSOR_DATA: {
                        sendSensorData(from);
                        break;
                    }
                    case INSTRUCTION_OPEN_VALVE: {
                        sendResponse(RESPONSE_OPENED_VALVE, F("Valve Opened"), from);
                        openValve();
                        break;
                    }
                    case INSTRUCTION_CLOSE_VALVE: {
                        sendResponse(RESPONSE_CLOSED_VALVE, F("Valve Closed"), from);
                        closeValve();
                        break;
                    }
                    default:
                        sendReceived(from);
                        Serial.println(F("Unknown Instruction Received"));
                }
                break;
            default:
                sendReceived(from);
        }
    } else {
        Serial.println(F("Receive failed"));
        reInitialiseNRF();
    }
}

unsigned long lastTime = 0;

void setup() {
    digitalWrite(VALVE_RELAY_PIN, HIGH);
    pinMode(VALVE_RELAY_PIN, OUTPUT);
    Serial.begin(9600);
    dallasTemperature.begin();
    while (!Serial);
    reInitialiseNRF();
    delay(1000);
    lastTime = millis();
}

void loop() {
    if (manager.available()) {
        readAndConsumeDataPacket();
    }
    if (millis() - lastTime > 5000) {
        lastTime = millis();
        reInitialiseNRF();
    }
}