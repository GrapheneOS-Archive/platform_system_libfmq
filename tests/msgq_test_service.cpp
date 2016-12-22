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

#include <android/hardware/tests/msgq/1.0/ITestMsgQ.h>
#include <hidl/ServiceManagement.h>
#include <hwbinder/IInterface.h>
#include <hwbinder/IPCThreadState.h>
#include <hwbinder/ProcessState.h>
#include <utils/Looper.h>
#include <fmq/MessageQueue.h>
#include <fmq/EventFlag.h>

// libutils:
using android::Looper;
using android::LooperCallback;
using android::OK;
using android::sp;

// libhwbinder:
using android::hardware::BnInterface;
using android::hardware::defaultServiceManager;
using android::hardware::IInterface;
using android::hardware::IPCThreadState;
using android::hardware::Parcel;
using android::hardware::ProcessState;
using android::hardware::Return;
using android::hardware::Void;

// Standard library
using std::string;
using std::vector;

// libhidl
using android::hardware::kSynchronizedReadWrite;
using android::hardware::kUnsynchronizedWrite;
using android::hardware::MQDescriptorSync;
using android::hardware::MQDescriptorUnsync;

using android::hardware::MessageQueue;

// Generated HIDL files
using android::hardware::tests::msgq::V1_0::ITestMsgQ;

const char kServiceName[] = "android.hardware.tests.msgq@1.0::ITestMsgQ";

namespace {

class BinderCallback : public LooperCallback {
public:
    BinderCallback() {}
    ~BinderCallback() override {}

    int handleEvent(int /* fd */, int /* events */, void* /* data */) override {
        IPCThreadState::self()->handlePolledCommands();
        return 1;  // Continue receiving callbacks.
    }
};

class TestMsgQ : public ITestMsgQ {
public:
      TestMsgQ()
              : mFmqSynchronized(nullptr), mFmqUnsynchronized(nullptr) {}

      virtual ~TestMsgQ() {
          delete mFmqSynchronized;
          delete mFmqUnsynchronized;
      }

      virtual Return<bool> requestWriteFmqSync(int count) {
          vector<uint16_t> data(count);
          for (int i = 0; i < count; i++) {
              data[i] = i;
          }
          bool result = mFmqSynchronized->write(&data[0], count);
          return result;
      }

      virtual Return<bool> requestReadFmqSync(int count) {
          vector<uint16_t> data(count);
          bool result = mFmqSynchronized->read(&data[0], count)
                  && verifyData(&data[0], count);
          return result;
      }

      virtual Return<bool> requestWriteFmqUnsync(int count) {
          vector<uint16_t> data(count);
          for (int i = 0; i < count; i++) {
              data[i] = i;
          }
          bool result = mFmqUnsynchronized->write(&data[0], count);
          return result;
      }

      virtual Return<bool> requestReadFmqUnsync(int count) {
          vector<uint16_t> data(count);
          bool result =
                  mFmqUnsynchronized->read(&data[0], count) && verifyData(&data[0], count);
          return result;
      }

      virtual Return<void> requestBlockingRead(int count) {
          vector<uint16_t> data(count);
          /*
           * Get the EventFlag word from MessageQueue and configure an
           * EventFlag object to use.
           */
          auto evFlagWordPtr = mFmqSynchronized->getEventFlagWord();
          android::hardware::EventFlag* efGroup = nullptr;
          android::status_t status = android::hardware::EventFlag::createEventFlag(evFlagWordPtr,
                                                                                   &efGroup);
          if ((status != android::NO_ERROR) || (efGroup == nullptr)) {
              ALOGE("Unable to configure EventFlag");
              return Void();
          }
          while (true) {
              uint32_t efState = 0;
              android::status_t ret = efGroup->wait(
                      static_cast<uint32_t>(ITestMsgQ::EventFlagBits::FMQ_NOT_EMPTY),
                      &efState,
                      NULL);

              if ((efState & ITestMsgQ::EventFlagBits::FMQ_NOT_EMPTY) &&
                  mFmqSynchronized->read(&data[0], count)) {
                  efGroup->wake(static_cast<uint32_t>(ITestMsgQ::EventFlagBits::FMQ_NOT_FULL));
                  break;
              }
          }
          return Void();
      }

      virtual Return<void> configureFmqSyncReadWrite(
              ITestMsgQ::configureFmqSyncReadWrite_cb callback) {
          static constexpr size_t kNumElementsInQueue = 1024;
          mFmqSynchronized =
                  new (std::nothrow) MessageQueue<uint16_t, kSynchronizedReadWrite>(
                          kNumElementsInQueue, true /* configureEventFlagWord */);
          if ((mFmqSynchronized == nullptr) || (mFmqSynchronized->isValid() == false)) {
              callback(false /* ret */, MQDescriptorSync(
                      std::vector<android::hardware::GrantorDescriptor>(),
                      nullptr /* nhandle */, 0 /* size */));
          } else {
              callback(true /* ret */, *mFmqSynchronized->getDesc());
          }
          return Void();
     }

      virtual Return<void> configureFmqUnsyncWrite(
              ITestMsgQ::configureFmqUnsyncWrite_cb callback) {
          static constexpr size_t kNumElementsInQueue = 1024;
          mFmqUnsynchronized =
                  new (std::nothrow) MessageQueue<uint16_t, kUnsynchronizedWrite>(
                          kNumElementsInQueue);
          if ((mFmqUnsynchronized == nullptr) ||
              (mFmqUnsynchronized->isValid() == false)) {
              callback(false /* ret */,
                       MQDescriptorUnsync(
                               std::vector<android::hardware::GrantorDescriptor>(),
                               nullptr /* nhandle */, 0 /* size */));
          } else {
              callback(true /* ret */, *mFmqUnsynchronized->getDesc());
          }
          return Void();
      }

      android::hardware::MessageQueue<uint16_t,
              android::hardware::kSynchronizedReadWrite>* mFmqSynchronized;
      android::hardware::MessageQueue<uint16_t,
              android::hardware::kUnsynchronizedWrite>* mFmqUnsynchronized;

private:
      /*
       * Utility function to verify data read from the fast message queue.
       */
      bool verifyData(uint16_t* data, int count) {
          for (int i = 0; i < count; i++) {
              if (data[i] != i) return false;
          }
          return true;
      }
};

int Run() {
    android::sp<TestMsgQ> service = new TestMsgQ;
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
        ALOGI("Looper returned %d", result);
    }
    return 0;
}

}  // namespace

int main(int /* argc */, char* /* argv */ []) { return Run(); }
