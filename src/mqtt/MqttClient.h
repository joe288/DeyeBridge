#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
// Disambiguate the Arduino wrapper from the library's case-colliding core header on Windows.
#include <../src/VictronBLE.h>
#include "../../include/DeyeRegisters.h"
#include "../core/RegisterCache.h"
#include "../core/WriteQueue.h"
#include "../web/Settings.h"

// MQTT client for Home Assistant: publishes sensor values, advertises the
// device via HA MQTT discovery, and accepts write commands on {base}/set/{key}
// which are pushed into the shared write queue (same path as evcc writes).
namespace DeyeBridge
{
    class MqttClient
    {
    public:
        void Init(WiFiClient &wifi, RegisterCache *cache, WriteQueue *queue,
                  const Settings &settings);

        // Call from the main loop: keeps the connection alive, publishes on
        // the configured interval and processes command messages
        void ProcessEvents();

        bool Connected() const { return const_cast<PubSubClient &>(mClient).connected(); }
        bool VictronConfigured() const { return mVictronConfigured; }
        bool VictronDataReceived() const
        {
            portENTER_CRITICAL(&mVictronMux);
            const bool received = mVictronSample.valid;
            portEXIT_CRITICAL(&mVictronMux);
            return received;
        }
        void PublishDiscovery();   // re-publish after config changes (debug)

    private:
        void Connect();
        void PublishValues();
        void AdvertiseDiscovery();
        void HandleCommand(char *topic, byte *payload, unsigned int length);
        void SendIfConnected(const char *topic, const char *payload, bool retain);
        void PublishVictronValues();
        void AdvertiseVictronDiscovery();
        void HandleVictronData(const VictronDevice *device);
        void UpdateVictronEnergy();
        void SaveVictronEnergy(bool force);
        static void VictronDataCallback(const VictronDevice *device);

        void Topic(char *out, size_t size, const char *suffix) const;
        void FormatValue(const DeyeRegisters::SensorEntity &entity, char *out, size_t size) const;

        struct VictronSample
        {
            float batteryVoltage;
            float batteryCurrent;
            float panelPower;
            uint16_t yieldToday;
            uint8_t chargeState;
            int8_t rssi;
            uint32_t lastUpdate;
            bool valid;
        };

        struct VictronEnergyState
        {
            uint32_t magic;
            char mac[13];
            uint8_t hasBaseline;
            uint16_t lastYieldToday;
            uint64_t totalYieldWh;
        };

        WiFiClient *mWifi = nullptr;
        PubSubClient mClient;
        RegisterCache *mCache = nullptr;
        WriteQueue *mQueue = nullptr;
        const Settings *mSettings = nullptr;

        char mBase[40];   // base topic with trailing slash
        char mVictronMac[13] = {};
        VictronBLE mVictron;
        Preferences mVictronPreferences;
        VictronSample mVictronSample = {};
        VictronEnergyState mVictronEnergy = {};
        mutable portMUX_TYPE mVictronMux = portMUX_INITIALIZER_UNLOCKED;
        uint32_t mNextPublishMs = 0;
        uint32_t mNextReconnectMs = 0;
        uint32_t mLastVictronEnergySaveMs = 0;
        uint32_t mLastProcessedVictronUpdate = 0;
        bool mDiscoverySent = false;
        bool mVictronConfigured = false;
        bool mVictronPreferencesReady = false;
        bool mVictronEnergyDirty = false;

        static MqttClient *sVictronCallbackClient;
    };
}
