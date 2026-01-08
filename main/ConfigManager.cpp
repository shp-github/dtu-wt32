#include "ConfigManager.h"
#include "SerialManager.h"
#include "NetworkManager.h"
#include <ArduinoJson.h>

namespace ConfigManager {

DeviceConfig deviceConfig;
static Preferences preferences;
static bool isInitialized = false;

void begin() {
    preferences.begin("device_config", false);

    // 设备名称
    deviceConfig.name = preferences.getString("basic_name", "");
    Serial.printf("Loaded device name: %s\n", deviceConfig.name.c_str());

    // 网络
    deviceConfig.ip          = preferences.getString("net_ip", "0.0.0.0");
    deviceConfig.subnet      = preferences.getString("net_subnet", "255.255.255.0");
    deviceConfig.gateway     = preferences.getString("net_gateway", "0.0.0.0");
    deviceConfig.dns         = preferences.getString("net_dns", "0.0.0.0");
    deviceConfig.isStatic    = preferences.getInt("net_is_static", 0);

    // 通道
    for (int i = 0; i < 3; i++) {
        String prefix = "ch" + String(i);
        deviceConfig.channels[i].enabled  = preferences.getBool((prefix + "_en").c_str(), false);
        deviceConfig.channels[i].protocol = preferences.getString((prefix + "_proto").c_str(), "");
        deviceConfig.channels[i].target   = preferences.getString((prefix + "_target").c_str(), "");
        deviceConfig.channels[i].port     = (uint16_t)preferences.getUInt((prefix + "_port").c_str(), 0);

        // 新增字段
        deviceConfig.channels[i].source = preferences.getString((prefix + "_source").c_str(), "");
        deviceConfig.channels[i].heartbeatTime = preferences.getInt((prefix + "_heartbeat").c_str(), 30);
        deviceConfig.channels[i].username = preferences.getString((prefix + "_username").c_str(), "");
        deviceConfig.channels[i].password = preferences.getString((prefix + "_password").c_str(), "");
        deviceConfig.channels[i].registerPackage = preferences.getString((prefix + "_regPkg").c_str(), "");
        deviceConfig.channels[i].heartbeatPackage = preferences.getString((prefix + "_heartPkg").c_str(), "");
        deviceConfig.channels[i].subscribeTopic = preferences.getString((prefix + "_subTopic").c_str(), "");
        deviceConfig.channels[i].publishTopic = preferences.getString((prefix + "_pubTopic").c_str(), "");
        deviceConfig.channels[i].clientID = preferences.getString((prefix + "_clientID").c_str(), "");
        deviceConfig.channels[i].QOS = preferences.getInt((prefix + "_qos").c_str(), 0);
        deviceConfig.channels[i].PubRetain = preferences.getBool((prefix + "_retain").c_str(), false);
        deviceConfig.channels[i].lastWillMessage = preferences.getString((prefix + "_willMsg").c_str(), "");
    }

    // 串口1
    deviceConfig.uart1.enabled  = preferences.getBool("uart1_en", true);
    deviceConfig.uart1.baud     = preferences.getInt("uart1_baud", 9600);
    deviceConfig.uart1.dataBits = preferences.getInt("uart1_dataBits", 8);
    deviceConfig.uart1.stopBits = preferences.getInt("uart1_stopBits", 1);
    deviceConfig.uart1.parity   = static_cast<SerialManager::ParityType>(preferences.getInt("uart1_parity", 0));

    // 串口2
    deviceConfig.uart2.enabled  = preferences.getBool("uart2_en", false);
    deviceConfig.uart2.baud     = preferences.getInt("uart2_baud", 9600);
    deviceConfig.uart2.dataBits = preferences.getInt("uart2_dataBits", 8);
    deviceConfig.uart2.stopBits = preferences.getInt("uart2_stopBits", 1);
    deviceConfig.uart2.parity   = static_cast<SerialManager::ParityType>(preferences.getInt("uart2_parity", 0));


    // Modbus
    deviceConfig.modbus.enabled = preferences.getBool("mb_en", false);
    deviceConfig.modbus.protocol = preferences.getString("mb_proto", "rtu");
    deviceConfig.modbus.inputSource = preferences.getString("mb_in_src", "serial1");
    deviceConfig.modbus.interval = preferences.getInt("mb_interval", 100);

    // outputSource 加载（使用逗号分隔字符串）
    deviceConfig.modbus.outputSource.clear();
    String outStr = preferences.getString("mb_out_src", "");
    if (outStr.length() > 0) {
        int startIdx = 0;
        int commaIdx;
        do {
            commaIdx = outStr.indexOf(',', startIdx);
            String numStr = (commaIdx == -1) ? outStr.substring(startIdx) : outStr.substring(startIdx, commaIdx);
            if (numStr.length() > 0) {
                deviceConfig.modbus.outputSource.push_back(numStr.toInt());
            }
            startIdx = commaIdx + 1;
        } while (commaIdx != -1);
    }

    // commands 加载（JSON 字符串）
    deviceConfig.modbus.commands.clear();
    String commandsStr = preferences.getString("mb_commands", "[]");
    if (commandsStr.length() > 0) {
        DynamicJsonDocument doc(4096);  // 根据实际大小调整
        DeserializationError error = deserializeJson(doc, commandsStr);
        if (!error) {
            JsonArray cmdArr = doc.as<JsonArray>();
            for (JsonObject c : cmdArr) {
                ModbusManager::Command cmd;
                cmd.a = c["a"].as<uint8_t>();
                cmd.f = c["f"].as<String>();
                cmd.r = c["r"].as<uint16_t>();
                cmd.s = c["s"].as<uint16_t>();

                // arr 数组
                if (c.containsKey("arr")) {
                    JsonArray mapArr = c["arr"].as<JsonArray>();
                    for (JsonObject mObj : mapArr) {
                        ModbusManager::Mapping map;
                        map.k = mObj["k"].as<String>();
                        map.a = mObj["a"].as<uint16_t>();
                        map.l = mObj["l"].as<uint16_t>();
                        map.o = mObj["o"].as<String>();
                        cmd.arr.push_back(map);
                    }
                }
                deviceConfig.modbus.commands.push_back(cmd);
            }
            Serial.printf("Loaded %d Modbus commands\n", deviceConfig.modbus.commands.size());
        } else {
            Serial.printf("Error parsing mb_commands JSON: %s\n", error.c_str());
        }
    }


    applyConfig();
}

void saveModule(const String &flag) {
    Preferences prefs;
    prefs.begin("device_config", false);

    if (flag == "interface") {
        prefs.putBool("uart1_en", deviceConfig.uart1.enabled);
        prefs.putInt("uart1_baud", deviceConfig.uart1.baud);
        prefs.putInt("uart1_dataBits", deviceConfig.uart1.dataBits);
        prefs.putInt("uart1_stopBits", deviceConfig.uart1.stopBits);
        prefs.putInt("uart1_parity", static_cast<int>(deviceConfig.uart1.parity));

        prefs.putBool("uart2_en", deviceConfig.uart2.enabled);
        prefs.putInt("uart2_baud", deviceConfig.uart2.baud);
        prefs.putInt("uart2_dataBits", deviceConfig.uart2.dataBits);
        prefs.putInt("uart2_stopBits", deviceConfig.uart2.stopBits);
        prefs.putInt("uart2_parity", static_cast<int>(deviceConfig.uart2.parity));
    }
    else if (flag == "channels") {
        for (int i = 0; i < 3; i++) {
            String prefix = "ch" + String(i);
            prefs.putBool((prefix + "_en").c_str(), deviceConfig.channels[i].enabled);
            prefs.putString((prefix + "_proto").c_str(), deviceConfig.channels[i].protocol);
            prefs.putString((prefix + "_target").c_str(), deviceConfig.channels[i].target);
            prefs.putUInt((prefix + "_port").c_str(), deviceConfig.channels[i].port);

            // 新增字段保存
            prefs.putString((prefix + "_source").c_str(), deviceConfig.channels[i].source);
            prefs.putInt((prefix + "_heartbeat").c_str(), deviceConfig.channels[i].heartbeatTime);
            prefs.putString((prefix + "_username").c_str(), deviceConfig.channels[i].username);
            prefs.putString((prefix + "_password").c_str(), deviceConfig.channels[i].password);
            prefs.putString((prefix + "_regPkg").c_str(), deviceConfig.channels[i].registerPackage);
            prefs.putString((prefix + "_heartPkg").c_str(), deviceConfig.channels[i].heartbeatPackage);
            prefs.putString((prefix + "_subTopic").c_str(), deviceConfig.channels[i].subscribeTopic);
            prefs.putString((prefix + "_pubTopic").c_str(), deviceConfig.channels[i].publishTopic);
            prefs.putString((prefix + "_clientID").c_str(), deviceConfig.channels[i].clientID);
            prefs.putInt((prefix + "_qos").c_str(), deviceConfig.channels[i].QOS);
            prefs.putBool((prefix + "_retain").c_str(), deviceConfig.channels[i].PubRetain);
            prefs.putString((prefix + "_willMsg").c_str(), deviceConfig.channels[i].lastWillMessage);
        }
    } else if (flag == "basic") {

        Serial.printf("[CONFIG] 保存基本配置\n");
        Serial.printf("[CONFIG] 设备名称: %s\n", deviceConfig.name.c_str());
        Serial.printf("[CONFIG] 网络配置: ip=%s, subnet=%s, gateway=%s, dns=%s, isStatic=%d\n",
                     deviceConfig.ip.c_str(),
                     deviceConfig.subnet.c_str(),
                     deviceConfig.gateway.c_str(),
                     deviceConfig.dns.c_str(),
                     deviceConfig.isStatic);

        // 保存设备名称
        prefs.putString("basic_name", deviceConfig.name);

        // 保存网络配置
        prefs.putString("net_ip", deviceConfig.ip);
        prefs.putString("net_subnet", deviceConfig.subnet);
        prefs.putString("net_gateway", deviceConfig.gateway);
        prefs.putString("net_dns", deviceConfig.dns);
        prefs.putInt("net_is_static", deviceConfig.isStatic);

        Serial.println("[CONFIG] 基本配置保存完成");

    } else if (flag == "modbus") {
        // 基础字段
        prefs.putBool("mb_en", deviceConfig.modbus.enabled);
        prefs.putString("mb_proto", deviceConfig.modbus.protocol);
        prefs.putString("mb_in_src", deviceConfig.modbus.inputSource);
        prefs.putInt("mb_interval", deviceConfig.modbus.interval);

        // outputSource 保存为逗号分隔字符串
        String outStr = "";
        for (size_t i = 0; i < deviceConfig.modbus.outputSource.size(); i++) {
            outStr += String(deviceConfig.modbus.outputSource[i]);
            if (i != deviceConfig.modbus.outputSource.size() - 1) outStr += ",";
        }
        prefs.putString("mb_out_src", outStr);

        // commands 保存为 JSON 字符串
        DynamicJsonDocument doc(4096);  // 根据实际需要调整大小
        JsonArray cmdArr = doc.to<JsonArray>();

        for (auto &cmd : deviceConfig.modbus.commands) {
            JsonObject c = cmdArr.createNestedObject();
            c["a"] = cmd.a;
            c["f"] = cmd.f;
            c["r"] = cmd.r;
            c["s"] = cmd.s;

            JsonArray mapArr = c.createNestedArray("arr");
            for (auto &m : cmd.arr) {
                JsonObject mo = mapArr.createNestedObject();
                mo["k"] = m.k;
                mo["a"] = m.a;
                mo["l"] = m.l;
                mo["o"] = m.o;
            }
        }

        String commandsStr;
        serializeJson(cmdArr, commandsStr);
        prefs.putString("mb_commands", commandsStr);
        Serial.printf("Saved %d Modbus commands, JSON size: %d\n",
                     deviceConfig.modbus.commands.size(), commandsStr.length());
    }

    prefs.end();
}

// 更新模块
void updateModuleFromJson(const JsonVariant &doc) {
    if (!doc.is<JsonObject>()) return;
    JsonObject obj = doc.as<JsonObject>();
    if (!obj.containsKey("flag")) return;

    String flag = obj["flag"].as<String>();

    if (flag == "interface") {
        if (obj.containsKey("uart1")) {
            JsonObject u1 = obj["uart1"].as<JsonObject>();
            if (u1.containsKey("enabled"))  deviceConfig.uart1.enabled  = u1["enabled"].as<bool>();
            if (u1.containsKey("baud"))     deviceConfig.uart1.baud     = u1["baud"].as<int>();
            if (u1.containsKey("dataBits")) deviceConfig.uart1.dataBits = u1["dataBits"].as<int>();
            if (u1.containsKey("stopBits")) deviceConfig.uart1.stopBits = u1["stopBits"].as<int>();
            if (u1.containsKey("parity"))   deviceConfig.uart1.parity   = static_cast<SerialManager::ParityType>(u1["parity"].as<int>());
            if (u1.containsKey("name"))     deviceConfig.uart1.name     = u1["name"].as<const char*>();
        }

        if (obj.containsKey("uart2")) {
            JsonObject u2 = obj["uart2"].as<JsonObject>();
            if (u2.containsKey("enabled"))  deviceConfig.uart2.enabled  = u2["enabled"].as<bool>();
            if (u2.containsKey("baud"))     deviceConfig.uart2.baud     = u2["baud"].as<int>();
            if (u2.containsKey("dataBits")) deviceConfig.uart2.dataBits = u2["dataBits"].as<int>();
            if (u2.containsKey("stopBits")) deviceConfig.uart2.stopBits = u2["stopBits"].as<int>();
            if (u2.containsKey("parity"))   deviceConfig.uart2.parity   = static_cast<SerialManager::ParityType>(u2["parity"].as<int>());
            if (u2.containsKey("name"))     deviceConfig.uart2.name     = u2["name"].as<const char*>();
        }
    }
     else if (flag == "channels") {
        if (!obj.containsKey("channels")) return;
        JsonArray arr = obj["channels"].as<JsonArray>();
        for (int i = 0; i < 3 && i < arr.size(); i++) {
            JsonObject ch = arr[i].as<JsonObject>();
            if (ch.containsKey("enabled"))  deviceConfig.channels[i].enabled  = ch["enabled"].as<bool>();
            if (ch.containsKey("protocol")) deviceConfig.channels[i].protocol = ch["protocol"].as<const char*>();
            if (ch.containsKey("target"))   deviceConfig.channels[i].target   = ch["target"].as<const char*>();
            if (ch.containsKey("port"))     deviceConfig.channels[i].port     = ch["port"].as<uint16_t>();

            // 新增字段处理
            if (ch.containsKey("source"))         deviceConfig.channels[i].source = ch["source"].as<const char*>();
            if (ch.containsKey("heartbeatTime"))  deviceConfig.channels[i].heartbeatTime = ch["heartbeatTime"].as<int>();
            if (ch.containsKey("username"))       deviceConfig.channels[i].username = ch["username"].as<const char*>();
            if (ch.containsKey("password"))       deviceConfig.channels[i].password = ch["password"].as<const char*>();
            if (ch.containsKey("registerPackage")) deviceConfig.channels[i].registerPackage = ch["registerPackage"].as<const char*>();
            if (ch.containsKey("heartbeatPackage")) deviceConfig.channels[i].heartbeatPackage = ch["heartbeatPackage"].as<const char*>();
            if (ch.containsKey("subscribeTopic")) deviceConfig.channels[i].subscribeTopic = ch["subscribeTopic"].as<const char*>();
            if (ch.containsKey("publishTopic"))   deviceConfig.channels[i].publishTopic = ch["publishTopic"].as<const char*>();
            if (ch.containsKey("clientID"))       deviceConfig.channels[i].clientID = ch["clientID"].as<const char*>();
            if (ch.containsKey("QOS"))            deviceConfig.channels[i].QOS = ch["QOS"].as<int>();
            if (ch.containsKey("PubRetain"))      deviceConfig.channels[i].PubRetain = ch["PubRetain"].as<bool>();
            if (ch.containsKey("lastWillMessage")) deviceConfig.channels[i].lastWillMessage = ch["lastWillMessage"].as<const char*>();
        }
    } else if (flag == "basic") {
      Serial.println("[CONFIG] 🛠️ 处理基本配置");

        bool hasChanges = false;

        // 设备名称
        if (obj.containsKey("name")) {
            String newName = obj["name"].as<const char*>();
            if (newName != deviceConfig.name) {
                deviceConfig.name = newName;
                hasChanges = true;
                Serial.printf("[CONFIG] 📝 更新设备名称: %s\n", deviceConfig.name.c_str());
            }
        }

        // 网络配置
        if (obj.containsKey("ip")) {
            String newIp = obj["ip"].as<const char*>();
            if (newIp != deviceConfig.ip) {
                deviceConfig.ip = newIp;
                hasChanges = true;
                Serial.printf("[CONFIG] 🌐 更新IP地址: %s\n", deviceConfig.ip.c_str());
            }
        }

        if (obj.containsKey("subnet")) {
            String newSubnet = obj["subnet"].as<const char*>();
            if (newSubnet != deviceConfig.subnet) {
                deviceConfig.subnet = newSubnet;
                hasChanges = true;
            }
        }

        if (obj.containsKey("gateway")) {
            String newGateway = obj["gateway"].as<const char*>();
            if (newGateway != deviceConfig.gateway) {
                deviceConfig.gateway = newGateway;
                hasChanges = true;
            }
        }

        if (obj.containsKey("dns")) {
            String newDns = obj["dns"].as<const char*>();
            if (newDns != deviceConfig.dns) {
                deviceConfig.dns = newDns;
                hasChanges = true;
            }
        }

        if (obj.containsKey("isStatic")) {
            int newIsStatic = obj["isStatic"].as<int>();
            if (newIsStatic != deviceConfig.isStatic) {
                deviceConfig.isStatic = newIsStatic;
                hasChanges = true;
                Serial.printf("[CONFIG] ⚡ 更新静态IP模式: %d\n", deviceConfig.isStatic);
            }
        }

        if (hasChanges) {
            saveModule("basic");
            Serial.println("[CONFIG] ✅ 基本配置已保存");
        } else {
            Serial.println("[CONFIG] ⚠️ 没有变化，跳过保存");
        }

    } else if (flag == "modbus") {
        if (!obj.containsKey("data")) return;
        JsonObject d = obj["data"].as<JsonObject>();

        // 基础字段
        if (d.containsKey("enabled")) deviceConfig.modbus.enabled = d["enabled"].as<bool>();
        if (d.containsKey("protocol")) deviceConfig.modbus.protocol = d["protocol"].as<const char*>();
        if (d.containsKey("inputSource")) deviceConfig.modbus.inputSource = d["inputSource"].as<const char*>();
        if (d.containsKey("interval")) deviceConfig.modbus.interval = d["interval"].as<uint16_t>();

        // outputSource 数组
        if (d.containsKey("outputSource")) {
            deviceConfig.modbus.outputSource.clear();
            JsonArray arr = d["outputSource"].as<JsonArray>();
            for (auto val : arr) {
                deviceConfig.modbus.outputSource.push_back(val.as<uint8_t>());
            }
        }

        // commands 数组
        if (d.containsKey("commands")) {
            deviceConfig.modbus.commands.clear();
            JsonArray cmdArr = d["commands"].as<JsonArray>();
            for (auto cmdJson : cmdArr) {
                ModbusManager::Command cmd;
                JsonObject c = cmdJson.as<JsonObject>();

                if (c.containsKey("a")) cmd.a = c["a"].as<uint8_t>();
                if (c.containsKey("f")) cmd.f = c["f"].as<const char*>();
                if (c.containsKey("r")) cmd.r = c["r"].as<uint16_t>();
                if (c.containsKey("s")) cmd.s = c["s"].as<uint16_t>();

                // arr 数组
                if (c.containsKey("arr")) {
                    JsonArray mapArr = c["arr"].as<JsonArray>();
                    for (auto m : mapArr) {
                        ModbusManager::Mapping map;
                        JsonObject mObj = m.as<JsonObject>();
                        if (mObj.containsKey("k")) map.k = mObj["k"].as<const char*>();
                        if (mObj.containsKey("a")) map.a = mObj["a"].as<uint16_t>();
                        if (mObj.containsKey("l")) map.l = mObj["l"].as<uint16_t>();
                        if (mObj.containsKey("o")) map.o = mObj["o"].as<const char*>();
                        cmd.arr.push_back(map);
                    }
                }
                deviceConfig.modbus.commands.push_back(cmd);
            }
            Serial.printf("Parsed %d Modbus commands from JSON\n", deviceConfig.modbus.commands.size());
        }
    }

    saveModule(flag);

}

DeviceConfig& getConfig() { return deviceConfig; }

// 模块化序列化
JsonObject serializeModule(JsonDocument &doc, const String &flag) {
    JsonObject root = doc.to<JsonObject>();
    root["flag"] = flag;

    if (flag == "interface") {
        JsonObject u1 = root.createNestedObject("uart1");
        u1["name"]     = deviceConfig.uart1.name;
        u1["enabled"]  = deviceConfig.uart1.enabled;
        u1["baud"]     = deviceConfig.uart1.baud;
        u1["dataBits"] = deviceConfig.uart1.dataBits;
        u1["stopBits"] = deviceConfig.uart1.stopBits;
        u1["parity"]   = static_cast<int>(deviceConfig.uart1.parity);

        JsonObject u2 = root.createNestedObject("uart2");
        u2["name"]     = deviceConfig.uart2.name;
        u2["enabled"]  = deviceConfig.uart2.enabled;
        u2["baud"]     = deviceConfig.uart2.baud;
        u2["dataBits"] = deviceConfig.uart2.dataBits;
        u2["stopBits"] = deviceConfig.uart2.stopBits;
        u2["parity"]   = static_cast<int>(deviceConfig.uart2.parity);
    }
    else if (flag == "channels") {
        JsonArray arr = root.createNestedArray("channels");
        for (int i = 0; i < 3; i++) {
            JsonObject ch = arr.createNestedObject();
            ch["enabled"]  = deviceConfig.channels[i].enabled;
            ch["protocol"] = deviceConfig.channels[i].protocol;
            ch["target"]   = deviceConfig.channels[i].target;
            ch["port"]     = deviceConfig.channels[i].port;

            // 新增字段序列化
            ch["source"] = deviceConfig.channels[i].source;
            ch["heartbeatTime"] = deviceConfig.channels[i].heartbeatTime;
            ch["username"] = deviceConfig.channels[i].username;
            ch["password"] = deviceConfig.channels[i].password;
            ch["registerPackage"] = deviceConfig.channels[i].registerPackage;
            ch["heartbeatPackage"] = deviceConfig.channels[i].heartbeatPackage;
            ch["subscribeTopic"] = deviceConfig.channels[i].subscribeTopic;
            ch["publishTopic"] = deviceConfig.channels[i].publishTopic;
            ch["clientID"] = deviceConfig.channels[i].clientID;
            ch["QOS"] = deviceConfig.channels[i].QOS;
            ch["PubRetain"] = deviceConfig.channels[i].PubRetain;
            ch["lastWillMessage"] = deviceConfig.channels[i].lastWillMessage;
        }
    } else if (flag == "basic") {
        root["name"]     = deviceConfig.name;
        root["ip"]       = deviceConfig.ip;
        root["subnet"]   = deviceConfig.subnet;
        root["gateway"]  = deviceConfig.gateway;
        root["dns"]      = deviceConfig.dns;
        root["isStatic"] = deviceConfig.isStatic;
    } else if (flag == "modbus") {
        JsonObject data = root.createNestedObject("data");

        data["enabled"] = deviceConfig.modbus.enabled;
        data["protocol"] = deviceConfig.modbus.protocol;
        data["inputSource"] = deviceConfig.modbus.inputSource;
        data["interval"] = deviceConfig.modbus.interval;

        // outputSource 数组
        JsonArray outArr = data.createNestedArray("outputSource");
        for (auto val : deviceConfig.modbus.outputSource) {
            outArr.add(val);
        }

        // commands 数组
        JsonArray cmdArr = data.createNestedArray("commands");
        for (auto &cmd : deviceConfig.modbus.commands) {
            JsonObject c = cmdArr.createNestedObject();
            c["a"] = cmd.a;
            c["f"] = cmd.f;
            c["r"] = cmd.r;
            c["s"] = cmd.s;

            // arr 数组
            JsonArray mapArr = c.createNestedArray("arr");
            for (auto &map : cmd.arr) {
                JsonObject mObj = mapArr.createNestedObject();
                mObj["k"] = map.k;
                mObj["a"] = map.a;
                mObj["l"] = map.l;
                mObj["o"] = map.o;
            }
        }
    }

    return root;
}

// 应用配置 - 只调用其他模块的applyConfig
void applyConfig() {
    SerialManager::applyConfig();
    NetworkManager::applyNetworkConfig();
    ModbusManager::applyConfig(deviceConfig.modbus);
}

} // namespace ConfigManager