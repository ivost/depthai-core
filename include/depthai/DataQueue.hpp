#pragma once

// std
#include <atomic>
#include <memory>

// project
#include "LockingQueue.hpp"
#include "xlink/XLinkConnection.hpp"

// shared
#include "depthai-shared/datatype/RawBuffer.hpp"

namespace dai {

// DataQueue presents a way to access data coming from MyriadX
class DataOutputQueue {
    std::shared_ptr<XLinkConnection> connection;
    LockingQueue<std::shared_ptr<RawBuffer>> queue;
    std::thread readingThread;
    std::atomic<bool> running{true};
    std::string exceptionMessage;

   public:
    DataOutputQueue(std::shared_ptr<XLinkConnection> conn, const std::string& streamName, unsigned int maxSize = 60, bool overwrite = false);
    ~DataOutputQueue();

    template <class T>
    bool has() {
        if(!running) throw std::runtime_error(exceptionMessage.c_str());
        std::shared_ptr<RawBuffer> val = nullptr;
        if(queue.front(val) && dynamic_cast<T*>(val.get())) {
            return true;
        }
        return false;
    }

    bool has() {
        if(!running) throw std::runtime_error(exceptionMessage.c_str());
        return !queue.empty();
    }

    template <class T>
    std::shared_ptr<T> tryGet() {
        if(!running) throw std::runtime_error(exceptionMessage.c_str());
        std::shared_ptr<RawBuffer> val = nullptr;
        if(!queue.tryPop(val)) return nullptr;
        return std::dynamic_pointer_cast<T>(val);
    }

    std::shared_ptr<RawBuffer> tryGet() {
        if(!running) throw std::runtime_error(exceptionMessage.c_str());
        std::shared_ptr<RawBuffer> p = nullptr;
        if(!queue.tryPop(p)) {
            return nullptr;
        }
        return p;
    }

    template <class T>
    std::shared_ptr<T> get() {
        if(!running) throw std::runtime_error(exceptionMessage.c_str());
        std::shared_ptr<RawBuffer> val = nullptr;
        queue.waitAndPop(val);
        return std::dynamic_pointer_cast<T>(val);
    }

    std::shared_ptr<RawBuffer> get() {
        if(!running) throw std::runtime_error(exceptionMessage.c_str());
        std::shared_ptr<RawBuffer> val = nullptr;
        queue.waitAndPop(val);
        return val;
    }

    template <class T, typename Rep, typename Period>
    std::shared_ptr<T> get(std::chrono::duration<Rep, Period> timeout) {
        if(!running) throw std::runtime_error(exceptionMessage.c_str());
        std::shared_ptr<RawBuffer> val = nullptr;
        if(!queue.tryWaitAndPop(val, timeout)) {
            return nullptr;
        }
        return std::dynamic_pointer_cast<T>(val);
    }

    template <typename Rep, typename Period>
    std::shared_ptr<RawBuffer> get(std::chrono::duration<Rep, Period> timeout) {
        if(!running) throw std::runtime_error(exceptionMessage.c_str());
        std::shared_ptr<RawBuffer> val = nullptr;
        queue.tryWaitAndPop(val, timeout);
        return val;
    }
};

// DataInputQueue presents a way to write to MyriadX
class DataInputQueue {
    std::shared_ptr<XLinkConnection> connection;
    LockingQueue<std::shared_ptr<RawBuffer>> queue;
    std::thread writingThread;
    std::atomic<bool> running{true};
    std::string name;
    std::string exceptionMessage;

   public:
    DataInputQueue(std::shared_ptr<XLinkConnection> conn, const std::string& streamName, unsigned int maxSize = 60, bool overwrite = false);
    ~DataInputQueue();

    void send(const std::shared_ptr<RawBuffer>& val);
    void sendSync(const std::shared_ptr<RawBuffer>& val);

    template <typename Rep, typename Period>
    bool send(const std::shared_ptr<RawBuffer>& val, std::chrono::duration<Rep, Period> timeout) {
        if(!running) throw std::runtime_error(exceptionMessage.c_str());
        return queue.tryWaitAndPush(val, timeout);
    }
};

}  // namespace dai
