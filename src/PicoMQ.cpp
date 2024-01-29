#if defined(ESP32)
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#error "This board is not supported."
#endif

#include "PicoMQ.h"

bool PicoMQ::topic_matches(const char * p, const char * t) {
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

String PicoMQ::get_topic_element(const char * topic, size_t index) {
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

String PicoMQ::get_topic_element(const String & topic, size_t index) {
    return get_topic_element(topic.c_str(), index);
}

void PicoMQ::begin() {
#if defined(ESP32)
    udp.beginMulticast(address, port);
#elif defined(ESP8266)
    udp.beginMulticast(IP_ADDR_ANY, address, port);
#else
#error "This board is not supported."
#endif
}

void PicoMQ::loop() {
    for (int i = 0; i < 16; ++i) {
        const size_t packet_size = udp.parsePacket();

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

        if (topic_len + 2 > packet_size) {
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

PicoMQ::Publish PicoMQ::begin_publish(const char * topic) {
#if defined(ESP32)
    udp.beginMulticastPacket();
#elif defined(ESP8266)
    udp.beginPacketMulticast(address, port, WiFi.localIP(), ttl);
#else
#error "This board is not supported."
#endif
    udp.write(80);
    do {
        udp.write((uint8_t) *topic);
    } while (*topic++);
    return Publish(*this);
}

PicoMQ::Publish PicoMQ::begin_publish(const String & topic) {
    return begin_publish(topic.c_str());
}

void PicoMQ::publish(const char * topic, const void * payload, size_t size) {
    auto publish = begin_publish(topic);
    publish.write((const uint8_t *) payload, size);
    publish.send();
}

void PicoMQ::subscribe(const String & topic_filter, MessageCallback callback) {
    subscriptions[topic_filter] = callback;
}

void PicoMQ::subscribe(const String & topic_filter, std::function<void(char * topic, char * payload)> callback) {
    subscribe(topic_filter, [callback](char * topic, void * payload, size_t payload_size) { callback(topic, (char *) payload); });
}

void PicoMQ::subscribe(const String & topic_filter, std::function<void(char * payload)> callback) {
    subscribe(topic_filter, [callback](char * topic, void * payload, size_t payload_size) { callback((char *) payload); });
}

void PicoMQ::subscribe(const String & topic_filter, std::function<void(void * payload, size_t payload_size)> callback) {
    subscribe(topic_filter, [callback](char * topic, void * payload, size_t payload_size) { callback(payload, payload_size); });
}

void PicoMQ::unsubscribe(const String & topic_filter) {
    subscriptions.erase(topic_filter);
}

PicoMQ::Publish::Publish(PicoMQ & picomq): picomq(picomq), send_pending(true) {}

PicoMQ::Publish::Publish(Publish && other): picomq(other.picomq), send_pending(other.send_pending) {
    other.send_pending = false;
}

PicoMQ::Publish::~Publish() {
    send();
}

size_t PicoMQ::Publish::write(const uint8_t * data, size_t length) {
    return picomq.udp.write(data, length);
}

size_t PicoMQ::Publish::write(uint8_t value) { return picomq.udp.write(value); }

void PicoMQ::Publish::send() {
    if (send_pending) {
        picomq.udp.endPacket();
        send_pending = false;
    }
}
