#pragma once

#include <Arduino.h>

#include <WiFiUdp.h>

#include <map>

class PicoMQ {
    public:
        static bool topic_matches(const char * p, const char * t);
        static String get_topic_element(const char * topic, size_t index);
        static String get_topic_element(const String & topic, size_t index);

        PicoMQ(const IPAddress & address = IPAddress(224, 0, 1, 80), uint16_t port = 1880, uint8_t ttl = 1):
            address(address), port(port), ttl(ttl) {}

        IPAddress address;
        uint16_t port;
        uint8_t ttl;

        void begin();
        void loop();

        // Publishing
        void publish(const char * topic, const void * payload, size_t size);

        template <typename T>
        void publish(const char * topic, T payload) {
            auto publish = begin_publish(topic);
            publish.print(payload);
            publish.send();
        }

        class Publish: public Print {
            public:
                Publish(PicoMQ & picomq);

                Publish(const Publish &) = delete;
                Publish & operator=(const Publish &) = delete;
                Publish(Publish && other);

                ~Publish();

                virtual size_t write(const uint8_t * data, size_t length) override;
                virtual size_t write(uint8_t value) override final;

                void send();

            protected:
                PicoMQ & picomq;
                bool send_pending;
        };

        Publish begin_publish(const char * topic);
        Publish begin_publish(const String & topic);

        template <typename T>
        void publish(const String & topic, T payload) { publish(topic.c_str(), payload); }

        // Subscriptions
        typedef std::function<void(char * topic, void * payload, size_t size)> MessageCallback;

        void subscribe(const String & topic_filter, MessageCallback callback);
        void subscribe(const String & topic_filter, std::function<void(char * topic, char * payload)> callback);
        void subscribe(const String & topic_filter, std::function<void(char * payload)> callback);
        void subscribe(const String & topic_filter, std::function<void(void * payload, size_t payload_size)> callback);
        void unsubscribe(const String & topic_filter);

    protected:
        WiFiUDP udp;
        std::map<String, MessageCallback> subscriptions;
};
