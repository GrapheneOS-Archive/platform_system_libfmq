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

#define LOG_TAG "FMQ_UnitTests"

#include <android/hardware/tests/msgq/1.0/ITestMsgQ.h>
#include <hidl/ServiceManagement.h>
#include <hidl/HidlTransportSupport.h>
#include <utils/Looper.h>
#include <fmq/MessageQueue.h>
#include <fmq/EventFlag.h>

// libutils:
using android::Looper;
using android::LooperCallback;
using android::OK;
using android::sp;

// libhidl:
using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;
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
          bool result = mFmqSynchronized->readBlocking(
                  &data[0],
                  count,
                  static_cast<uint32_t>(ITestMsgQ::EventFlagBits::FMQ_NOT_FULL),
                  static_cast<uint32_t>(ITestMsgQ::EventFlagBits::FMQ_NOT_EMPTY),
                  5000000000 /* timeOutNanos */);

          if (result == false) {
              ALOGE("Blocking read fails");
          }
          return Void();
      }

      virtual Return<void> requestBlockingReadRepeat(int count, int numIter) {
          vector<uint16_t> data(count);
          for (int i = 0; i < numIter; i++) {
              bool result = mFmqSynchronized->readBlocking(
                      &data[0],
                      count,
                      static_cast<uint32_t>(ITestMsgQ::EventFlagBits::FMQ_NOT_FULL),
                      static_cast<uint32_t>(ITestMsgQ::EventFlagBits::FMQ_NOT_EMPTY),
                      5000000000 /* timeOutNanos */);

              if (result == false) {
                  ALOGE("Blocking read fails");
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
              callback(false /* ret */, MQDescriptorSync<uint16_t>());
          } else {
              /*
               * Initialize the EventFlag word with bit FMQ_NOT_FULL.
               */
              auto evFlagWordPtr = mFmqSynchronized->getEventFlagWord();
              if (evFlagWordPtr != nullptr) {
                  std::atomic_init(evFlagWordPtr,
                                   static_cast<uint32_t>(ITestMsgQ::EventFlagBits::FMQ_NOT_FULL));
              }
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
              callback(false /* ret */, MQDescriptorUnsync<uint16_t>());
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

    configureRpcThreadpool(1, true /*callerWillJoin*/);
    service->registerAsService(kServiceName);
    joinRpcThreadpool();

    return 0;
}

}  // namespace

int main(int /* argc */, char* /* argv */ []) { return Run(); }
