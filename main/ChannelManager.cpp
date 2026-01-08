#include "ChannelManager.h"

namespace ChannelManager {

static Protocol::BaseProtocol* channels[3] = {nullptr, nullptr, nullptr};
static ChannelConfig channelConfigs[3];

void begin() {
    // 初始化时所有通道都为空
    for (int i = 0; i < 3; i++) {
        channelConfigs[i] = ChannelConfig();
    }
}

void updateFromJson(const JsonVariant &doc) {
    if (!doc.is<JsonObject>()) return;
    JsonObject obj = doc.as<JsonObject>();

    if (!obj.containsKey("channels")) return;
    JsonArray arr = obj["channels"].as<JsonArray>();

    for (int i = 0; i < 3 && i < arr.size(); i++) {
        JsonObject ch = arr[i].as<JsonObject>();
        ChannelConfig config;

        config.enabled = ch["enabled"].as<bool>();
        config.source = ch["source"].as<const char*>();
        config.protocol = ch["protocol"].as<const char*>();
        config.target = ch.containsKey("ip") ? ch["ip"].as<const char*>() : ch["target"].as<const char*>();
        config.port = ch["port"].as<uint16_t>();
        config.heartbeatTime = ch["heartbeatTime"].as<int>();
        config.username = ch["username"].as<const char*>();
        config.password = ch["password"].as<const char*>();
        config.registerPackage = ch["registerPackage"].as<const char*>();
        config.heartbeatPackage = ch["heartbeatPackage"].as<const char*>();
        config.subscribeTopic = ch["subscribeTopic"].as<const char*>();
        config.publishTopic = ch["publishTopic"].as<const char*>();
        config.clientID = ch["clientID"].as<const char*>();
        config.QOS = ch["QOS"].as<int>();
        config.PubRetain = ch["PubRetain"].as<bool>();
        config.lastWillMessage = ch["lastWillMessage"].as<const char*>();

        // 停止现有通道
        if (channels[i]) {
            channels[i]->end();
            delete channels[i];
            channels[i] = nullptr;
        }

        // 创建新通道
        if (config.enabled) {
            if (config.protocol == "tcp") {
                channels[i] = new Protocol::TCPProtocol();
            } else if (config.protocol == "mqtt") {
                channels[i] = new Protocol::MQTTProtocol();
            }

            if (channels[i]) {
                Protocol::ProtocolConfig protoConfig;
                protoConfig.enabled = config.enabled;
                protoConfig.source = config.source;
                protoConfig.protocol = (config.protocol == "tcp") ?
                    Protocol::ProtocolType::TCP : Protocol::ProtocolType::MQTT;
                protoConfig.target = config.target;
                protoConfig.port = config.port;
                protoConfig.heartbeatTime = config.heartbeatTime;
                protoConfig.username = config.username;
                protoConfig.password = config.password;
                protoConfig.registerPackage = config.registerPackage;
                protoConfig.heartbeatPackage = config.heartbeatPackage;
                protoConfig.subscribeTopic = config.subscribeTopic;
                protoConfig.publishTopic = config.publishTopic;
                protoConfig.clientID = config.clientID;
                protoConfig.QOS = config.QOS;
                protoConfig.PubRetain = config.PubRetain;
                protoConfig.lastWillMessage = config.lastWillMessage;

                channels[i]->begin(protoConfig);
            }
        }

        channelConfigs[i] = config;
        Serial.printf("[Channel %d] Updated: %s %s://%s:%d\n",
                      i, config.source.c_str(), config.protocol.c_str(),
                      config.target.c_str(), config.port);
    }
}

void loop() {
    for (int i = 0; i < 3; i++) {
        if (channels[i]) {
            channels[i]->loop();
        }
    }
}

bool sendData(int channelIndex, const uint8_t* data, size_t length) {
    if (channelIndex < 0 || channelIndex >= 3) return false;
    if (!channels[channelIndex]) return false;

    return channels[channelIndex]->sendData(data, length);
}

bool sendData(const String& source, const uint8_t* data, size_t length) {
    for (int i = 0; i < 3; i++) {
        if (channels[i] && channelConfigs[i].source == source) {
            return channels[i]->sendData(data, length);
        }
    }
    return false;
}

JsonArray serializeChannels(JsonDocument &doc) {
    JsonArray arr = doc.createNestedArray("channels");

    for (int i = 0; i < 3; i++) {
        JsonObject ch = arr.createNestedObject();
        ch["enabled"] = channelConfigs[i].enabled;
        ch["source"] = channelConfigs[i].source;
        ch["protocol"] = channelConfigs[i].protocol;
        ch["ip"] = channelConfigs[i].target;
        ch["port"] = channelConfigs[i].port;
        ch["heartbeatTime"] = channelConfigs[i].heartbeatTime;
        ch["username"] = channelConfigs[i].username;
        ch["password"] = channelConfigs[i].password;
        ch["registerPackage"] = channelConfigs[i].registerPackage;
        ch["heartbeatPackage"] = channelConfigs[i].heartbeatPackage;
        ch["subscribeTopic"] = channelConfigs[i].subscribeTopic;
        ch["publishTopic"] = channelConfigs[i].publishTopic;
        ch["clientID"] = channelConfigs[i].clientID;
        ch["QOS"] = channelConfigs[i].QOS;
        ch["PubRetain"] = channelConfigs[i].PubRetain;
        ch["lastWillMessage"] = channelConfigs[i].lastWillMessage;
        ch["target"] = channelConfigs[i].target;

        // 添加连接状态
        if (channels[i]) {
            const auto& protoConfig = channels[i]->getConfig();
            ch["connected"] = protoConfig.connected;
        } else {
            ch["connected"] = false;
        }
    }

    return arr;
}

String getChannelStatus(int index) {
    if (index < 0 || index >= 3 || !channels[index]) {
        return "Disabled";
    }

    const auto& config = channels[index]->getConfig();
    if (config.connected) {
        return "Connected";
    } else {
        return "Disconnected";
    }
}

const ChannelConfig& getChannelConfig(int index) {
    return channelConfigs[index];
}

} // namespace ChannelManager