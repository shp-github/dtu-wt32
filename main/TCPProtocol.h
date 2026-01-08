#pragma once
#include "BaseProtocol.h"
#include <WiFi.h>
#include <WiFiClient.h>

namespace Protocol {

    class TCPProtocol : public BaseProtocol {
    public:
        TCPProtocol();
        ~TCPProtocol();

        bool begin(const ProtocolConfig& config) override;
        void end() override;
        bool sendData(const uint8_t* data, size_t length) override;
        bool isConnected() override;
        void loop() override;

    private:
        WiFiClient client;
        bool connect();
        void checkConnection();
    };

} // namespace Protocol