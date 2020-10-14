#include "depthai/DataQueue.hpp"

#include <iostream>

#include "datatype/StreamPacketParser.hpp"
#include "depthai-shared/xlink/XLinkConstants.hpp"

namespace dai {

DataOutputQueue::DataOutputQueue(std::shared_ptr<XLinkConnection> conn, const std::string& streamName, unsigned int maxSize, bool overwrite)
    : connection(std::move(conn)), queue(maxSize, overwrite) {
    // creates a thread which reads from connection into the queue
    readingThread = std::thread([this, streamName]() {
        std::uint64_t numPacketsRead = 0;

        try {
            // open stream with 1B write size (no writing will happen here)
            connection->openStream(streamName, 1);

            while(running) {
                // read packet
                auto* packet = connection->readFromStreamRaw(streamName);
                // parse packet
                auto data = parsePacket(packet);
                // release packet
                connection->readFromStreamRawRelease(streamName);
                // Add 'data' to queue
                queue.push(data);
                // Increment numPacketsRead
                numPacketsRead++;
            }

            connection->closeStream(streamName);

        } catch(const std::exception& ex) {
            // TODO(themarpe)
            std::cout << "Exception: " << ex.what() << std::endl;
            exceptionMessage = std::string(ex.what());
        }

        running = false;
    });
}

DataOutputQueue::~DataOutputQueue() {
    // detach from thread, because currently no way to unblock underlying XLinkReadData
    running = false;
    readingThread.detach();
}

DataInputQueue::DataInputQueue(std::shared_ptr<XLinkConnection> conn, const std::string& streamName, unsigned int maxSize, bool overwrite)
    : connection(std::move(conn)), queue(maxSize, overwrite) {
    // creates a thread which reads from connection into the queue
    writingThread = std::thread([this, streamName]() {
        std::uint64_t numPacketsSent = 0;

        try {
            // open stream with 1B write size (no writing will happen here)
            connection->openStream(streamName, XLINK_USB_BUFFER_MAX_SIZE);

            while(running) {
                // get data from queue
                std::shared_ptr<RawBuffer> data;
                queue.waitAndPop(data);

                // serialize
                auto serialized = serializeData(data);

                // Write packet to device
                connection->writeToStream(streamName, serialized);

                // Increment num packets sent
                numPacketsSent++;
            }

            connection->closeStream(streamName);

        } catch(const std::exception& ex) {
            // TODO(themarpe)
            std::cout << "Exception: " << ex.what() << std::endl;
            exceptionMessage = std::string(ex.what());
        }

        running = false;
    });
}

DataInputQueue::~DataInputQueue() {
    // detach from thread, because currently no way to unblock underlying XLinkWriteData
    running = false;
    writingThread.detach();
}

void DataInputQueue::send(const std::shared_ptr<RawBuffer>& val) {
    if(!running) throw std::runtime_error(exceptionMessage.c_str());
    queue.push(val);
}

void DataInputQueue::sendSync(const std::shared_ptr<RawBuffer>& val) {
    if(!running) throw std::runtime_error(exceptionMessage.c_str());
    queue.waitEmpty();
    queue.push(val);
}

}  // namespace dai
