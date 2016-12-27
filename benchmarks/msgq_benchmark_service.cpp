/*
* Copyright (C) 2016 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include <hidl/ServiceManagement.h>
#include <hwbinder/IInterface.h>
#include <hwbinder/IPCThreadState.h>
#include <hwbinder/ProcessState.h>
#include <utils/Looper.h>
#include <utils/StrongPointer.h>
#include <iostream>
#include <thread>

#include <fmq/MessageQueue.h>
#include <android/hardware/benchmarks/msgq/1.0/IBenchmarkMsgQ.h>

// libutils:
using android::Looper;
using android::LooperCallback;
using android::OK;
using android::sp;

// libhwbinder:
using android::hardware::defaultServiceManager;
using android::hardware::IInterface;
using android::hardware::IPCThreadState;
using android::hardware::Parcel;
using android::hardware::ProcessState;
using android::hardware::Return;
using android::hardware::Void;

// Standard library
using std::cerr;
using std::cout;
using std::endl;
using std::string;
using std::unique_ptr;
using std::vector;

// libhidl
using android::hardware::kSynchronizedReadWrite;
using android::hardware::MQFlavor;

// Generated HIDL files

using android::hardware::benchmarks::msgq::V1_0::IBenchmarkMsgQ;

/*
 * The various packet sizes used are as follows.
 */
enum PacketSizes {
    kPacketSize64 = 64,
    kPacketSize128 = 128,
    kPacketSize256 = 256,
    kPacketSize512 = 512,
    kPacketSize1024 = 1024
};

const char kServiceName[] = "android.hardware.benchmarks.msgq@1.0::IBenchmarkMsgQ";

namespace {
/*
 * This method writes numIter packets into the mFmqOutbox queue
 * and notes the time before each write in the mTimeData array. It will
 * be used to calculate the average server to client write to read delay.
 */
template <MQFlavor flavor>
void QueueWriter(android::hardware::MessageQueue<uint8_t, flavor>*
                 mFmqOutbox, int64_t* mTimeData, uint32_t numIter) {
    uint8_t data[kPacketSize64];
    uint32_t numWrites = 0;

    while (numWrites < numIter) {
        do {
            mTimeData[numWrites] =
                    std::chrono::high_resolution_clock::now().time_since_epoch().count();
        } while (mFmqOutbox->write(data, kPacketSize64) == false);
        numWrites++;
    }
}

/*
 * The method reads a packet from the inbox queue and writes the same
 * into the outbox queue. The client will calculate the average time taken
 * for each iteration which consists of two write and two read operations.
 */
template <MQFlavor flavor>
void QueuePairReadWrite(
        android::hardware::MessageQueue<uint8_t, flavor>* mFmqInbox,
        android::hardware::MessageQueue<uint8_t, flavor>* mFmqOutbox,
        uint32_t numIter) {
    uint8_t data[kPacketSize64];
    uint32_t numRoundTrips = 0;

    while (numRoundTrips < numIter) {
        while (mFmqInbox->read(data, kPacketSize64) == false)
          ;
        while (mFmqOutbox->write(data, kPacketSize64) == false)
          ;
        numRoundTrips++;
    }
}

class BinderCallback : public LooperCallback {
public:
    BinderCallback() {}
    ~BinderCallback() override {}

    int handleEvent(int /* fd */, int /* events */, void* /* data */) override {
        IPCThreadState::self()->handlePolledCommands();
        return 1;  // Continue receiving callbacks.
    }
};

class BenchmarkMsgQ : public IBenchmarkMsgQ {
public:
    BenchmarkMsgQ() :
        mFmqInbox(nullptr), mFmqOutbox(nullptr), mTimeData(nullptr) {}
    virtual ~BenchmarkMsgQ() {
        if (mFmqInbox) delete mFmqInbox;
        if (mFmqOutbox) delete mFmqOutbox;
        if (mTimeData) delete[] mTimeData;
    }

    virtual Return<void> benchmarkPingPong(uint32_t numIter) {
        std::thread(QueuePairReadWrite<kSynchronizedReadWrite>, mFmqInbox,
                    mFmqOutbox, numIter)
            .detach();
        return Void();
    }

    virtual Return<void> benchmarkServiceWriteClientRead(uint32_t numIter) {
        if (mTimeData) delete[] mTimeData;
        mTimeData = new (std::nothrow) int64_t[numIter];
        std::thread(QueueWriter<kSynchronizedReadWrite>, mFmqOutbox,
                    mTimeData, numIter).detach();
        return Void();
    }

    virtual Return<bool> requestWrite(int count) {
        uint8_t* data = new (std::nothrow) uint8_t[count];
        for (int i = 0; i < count; i++) {
          data[i] = i;
        }
        bool result = mFmqOutbox->write(data, count);
        delete[] data;
        return result;
    }

    virtual Return<bool> requestRead(int count) {
        uint8_t* data = new (std::nothrow) uint8_t[count];
        bool result = mFmqInbox->read(data, count);
        delete[] data;
        return result;
    }

    /*
     * This method is used by the client to send the server timestamps to
     * calculate the server to client write to read delay.
     */
    virtual Return<void> sendTimeData(
            const android::hardware::hidl_vec<int64_t>& clientRcvTimeArray) {
        int64_t accumulatedTime = 0;

        for (uint32_t i = 0; i < clientRcvTimeArray.size(); i++) {
            std::chrono::time_point<std::chrono::high_resolution_clock>
                    clientRcvTime((std::chrono::high_resolution_clock::duration(
                            clientRcvTimeArray[i])));
            std::chrono::time_point<std::chrono::high_resolution_clock>serverSendTime(
                    (std::chrono::high_resolution_clock::duration(mTimeData[i])));
            accumulatedTime += static_cast<int64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(clientRcvTime -
                                                                         serverSendTime).count());
        }

        accumulatedTime /= clientRcvTimeArray.size();
        cout << "Average service to client write to read delay::"
             << accumulatedTime << "ns" << endl;
        return Void();
    }

    /*
     * This method requests the service to configure the client's outbox queue.
     */
    virtual Return<void> configureClientOutboxSyncReadWrite(
            IBenchmarkMsgQ::configureClientOutboxSyncReadWrite_cb callback) {
        static constexpr size_t kNumElementsInQueue = 16 * 1024;
        mFmqInbox = new (std::nothrow) android::hardware::MessageQueue<uint8_t,
                          kSynchronizedReadWrite>(kNumElementsInQueue);
        if ((mFmqInbox == nullptr) || (mFmqInbox->isValid() == false)) {
            callback(false /* ret */, android::hardware::MQDescriptorSync<uint8_t>(
                           std::vector<android::hardware::GrantorDescriptor>(),
                           nullptr /* nhandle */, 0 /* size */));
        } else {
            callback(true /* ret */, *mFmqInbox->getDesc());
        }

        return Void();
    }

    /*
     * This method requests the service to configure the client's inbox queue.
     */
    virtual Return<void> configureClientInboxSyncReadWrite(
            IBenchmarkMsgQ::configureClientInboxSyncReadWrite_cb callback) {
        static constexpr size_t kNumElementsInQueue = 16 * 1024;
        mFmqOutbox = new (std::nothrow) android::hardware::MessageQueue<uint8_t,
                           kSynchronizedReadWrite>(kNumElementsInQueue);
        if (mFmqOutbox == nullptr) {
            callback(false /* ret */, android::hardware::MQDescriptorSync<uint8_t>(
                    std::vector<android::hardware::GrantorDescriptor>(),
                    nullptr /* nhandle */, 0 /* size */));
        } else {
            callback(true /* ret */, *mFmqOutbox->getDesc());
        }

        return Void();
    }

    android::hardware::MessageQueue<uint8_t, kSynchronizedReadWrite>* mFmqInbox;
    android::hardware::MessageQueue<uint8_t, kSynchronizedReadWrite>* mFmqOutbox;
    int64_t* mTimeData;
};

int Run() {
    android::sp<BenchmarkMsgQ> service = new BenchmarkMsgQ;
    sp<Looper> looper(Looper::prepare(0 /* opts */));
    int binderFd = -1;
    ProcessState::self()->setThreadPoolMaxThreadCount(0);
    IPCThreadState::self()->disableBackgroundScheduling(true);
    IPCThreadState::self()->setupPolling(&binderFd);

    if (binderFd < 0) return -1;

    sp<BinderCallback> cb(new BinderCallback);
    if (looper->addFd(binderFd, Looper::POLL_CALLBACK, Looper::EVENT_INPUT, cb,
                    nullptr) != 1) {
        ALOGE("Failed to add binder FD to Looper");
        return -1;
    }

    service->registerAsService(kServiceName);

    ALOGI("Entering loop");
    while (true) {
        const int result = looper->pollAll(-1 /* timeoutMillis */);
    }
    return 0;
}

}  // namespace

int main(int /* argc */, char* /* argv */ []) { return Run(); }
