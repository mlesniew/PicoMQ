#include <Arduino.h>

#include <WiFiUdp.h>

#include <map>

class PicoMQ {
    public:
        static bool topic_matches(const char * p, const char * t) {
            while (true) {
                switch (*p) {
                    case '\0':
                        // end of pattern reached
                        // TODO: check for '/#' suffix
                        return (*t == '\0');

                    case '#':
                        // multilevel wildcard
                        if (*t == '\0') {
                            return false;
                        }
                        return true;

                    case '+':
                        // single level wildcard
                        while (*t && *t != '/') {
                            ++t;
                        }
                        ++p;
                        break;

                    default:
                        // regular match
                        if (*p != *t) {
                            return false;
                        }
                        ++p;
                        ++t;
                }
            }
        }

        static String get_topic_element(const char * topic, size_t index) {
            while (index && topic[0]) {
                if (topic++[0] == '/') {
                    --index;
                }
            }

            if (!topic[0]) {
                return "";
            }

            const char * end = topic;
            while (*end && *end != '/') {
                ++end;
            }

            String ret;
            ret.concat(topic, end - topic);
            return ret;
        }

        static String get_topic_element(const String & topic, size_t index) {
            return get_topic_element(topic.c_str(), index);
        }

        PicoMQ(const IPAddress & address = IPAddress(224, 0, 1, 80), uint16_t port = 1880, uint8_t ttl = 1):
            address(address), port(port), ttl(ttl) {
        }

        IPAddress address;
        uint16_t port;
        uint8_t ttl;

        void begin() {
            udp.beginMulticast(IP_ADDR_ANY, address, port);
        }

        void loop() {
            for (int i = 0; i < 16; ++i) {
                const auto packet_size = udp.parsePacket();

                if (!packet_size) {
                    return;
                }

                if (udp.remoteIP() == WiFi.localIP()) {
                    return;
                }

                char buffer[packet_size + 1];

                for (size_t bytes_received = 0; bytes_received < packet_size;) {
                    const auto bytes_read = udp.read(buffer + bytes_received, packet_size - bytes_received);
                    if (bytes_read <= 0) {
                        // read error
                        return;
                    }
                    bytes_received += bytes_read;
                }

                // check length
                if ((packet_size <= 2) || (packet_size >= 1500)) {
                    // packet too short or too long
                    return;
                }

                // check magic byte
                if (buffer[0] != 80) {
                    // malformed packed, discard
                    return;
                }

                // ensure data ends with zero byte
                buffer[packet_size] = '\0';

                char * topic = buffer + 1;
                const auto topic_len = strlen(topic);

                if (topic_len > packet_size - 2) {
                    // no topic null terminator
                    return;
                }

                const auto payload_size = packet_size - topic_len - 2;
                char * payload = topic + topic_len + 1;

                // fire callbacks
                for (const auto & kv : subscriptions) {
                    if (topic_matches(kv.first.c_str(), topic)) {
                        kv.second(topic, payload, payload_size);
                    }
                }
            }
        }

        // Publishing
        void publish(const char * topic, const void * payload, size_t size) {
            write_header(topic);
            udp.write((const uint8_t *) payload, size);
            udp.endPacket();
        }

        template <typename T>
        void publish(const char * topic, T payload) {
            write_header(topic);
            udp.print(payload);
            udp.endPacket();
        }

        template <typename T>
        void publish(const String & topic, T payload) { publish(topic.c_str(), payload); }

        // Subscriptions
        typedef std::function<void(char * topic, void * payload, size_t size)> MessageCallback;

        void subscribe(const String & topic_filter, MessageCallback callback) {
            subscriptions[topic_filter] = callback;
        }

        void subscribe(const String & topic_filter, std::function<void(char * topic, char * payload)> callback) {
            subscribe(topic_filter, [callback](char * topic, void * payload, size_t payload_size) { callback(topic, (char *) payload); });
        }

        void subscribe(const String & topic_filter, std::function<void(char * payload)> callback) {
            subscribe(topic_filter, [callback](char * topic, void * payload, size_t payload_size) { callback((char *) payload); });
        }

        void subscribe(const String & topic_filter, std::function<void(void * payload, size_t payload_size)> callback) {
            subscribe(topic_filter, [callback](char * topic, void * payload, size_t payload_size) { callback(payload, payload_size); });
        }

        void unsubscribe(const String & topic_filter) {
            subscriptions.erase(topic_filter);
        }

    protected:
        WiFiUDP udp;
        std::map<String, MessageCallback> subscriptions;

        void write_header(const char * topic) {
            udp.beginPacketMulticast(address, port, WiFi.localIP(), ttl);

            udp.write(80);
            do {
                udp.write((uint8_t) *topic);
            } while (*topic++);
        }
};
