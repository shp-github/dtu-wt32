#pragma once
#include "BaseProtocol.h"
#include <PubSubClient.h>
#include <WiFi.h>

namespace Protocol {

    class MQTTProtocol : public BaseProtocol {
    public:
        MQTTProtocol();
        ~MQTTProtocol();

        bool begin(const ProtocolConfig& config) override;
        void end() override;
        bool sendData(const uint8_t* data, size_t length) override;
        bool isConnected() override;
        void loop() override;

    private:
        WiFiClient wifiClient;
        PubSubClient mqttClient;

        bool connect();
        void mqttCallback(char* topic, byte* payload, unsigned int length);
        static void staticMqttCallback(char* topic, byte* payload, unsigned int length);
    };

} // namespace Protocol