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

#ifndef HIDL_MQ_H
#define HIDL_MQ_H

#include <android-base/logging.h>
#include <atomic>
#include <cutils/ashmem.h>
#include <fmq/EventFlag.h>
#include <hidl/MQDescriptor.h>
#include <new>
#include <sys/mman.h>
#include <utils/Log.h>

namespace android {
namespace hardware {

template <typename T, MQFlavor flavor>
struct MessageQueue {
    /**
     * @param Desc MQDescriptor describing the FMQ.
     * @param resetPointers bool indicating whether the read/write pointers
     * should be reset or not.
     */
    MessageQueue(const MQDescriptor<T, flavor>& Desc, bool resetPointers = true);

    ~MessageQueue();

    /**
     * This constructor uses Ashmem shared memory to create an FMQ
     * that can contain a maximum of 'numElementsInQueue' elements of type T.
     *
     * @param numElementsInQueue Capacity of the MessageQueue in terms of T.
     * @param configureEventFlagWord Boolean that specifies if memory should
     * also be allocated and mapped for an EventFlag word.
     */
    MessageQueue(size_t numElementsInQueue, bool configureEventFlagWord = false);

    /**
     * @return Number of items of type T that can be written into the FMQ
     * without a read.
     */
    size_t availableToWrite() const;

    /**
     * @return Number of items of type T that are waiting to be read from the
     * FMQ.
     */
    size_t availableToRead() const;

    /**
     * Returns the size of type T in bytes.
     *
     * @param Size of T.
     */
    size_t getQuantumSize() const;

    /**
     * Returns the size of the FMQ in terms of the size of type T.
     *
     * @return Number of items of type T that will fit in the FMQ.
     */
    size_t getQuantumCount() const;

    /**
     * @return Whether the FMQ is configured correctly.
     */
    bool isValid() const;

    /**
     * Non-blocking write to FMQ.
     *
     * @param data Pointer to the object of type T to be written into the FMQ.
     *
     * @return Whether the write was successful.
     */
    bool write(const T* data);

    /**
     * Non-blocking read from FMQ.
     *
     * @param data Pointer to the memory where the object read from the FMQ is
     * copied to.
     *
     * @return Whether the read was successful.
     */
    bool read(T* data);

    /**
     * Write some data into the FMQ without blocking.
     *
     * @param data Pointer to the array of items of type T.
     * @param count Number of items in array.
     *
     * @return Whether the write was successful.
     */
    bool write(const T* data, size_t count);

    /**
     * Perform a blocking write of 'count' items into the FMQ using EventFlags.
     * Does not support partial writes.
     *
     * If 'evFlag' is nullptr, it is checked whether there is an EventFlag object
     * associated with the FMQ and it is used in that case.
     *
     * The application code must ensure that 'evFlag' used by the
     * reader(s)/writer is based upon the same EventFlag word.
     *
     * The method will return false without blocking if any of the following
     * conditions are true:
     * - If 'evFlag' is nullptr and the FMQ does not own an EventFlag object.
     * - If the flavor of the FMQ is synchronized and the 'readNotification' bit mask is zero.
     * - If 'count' is greater than the FMQ size.
     *
     * If the flavor of the FMQ is synchronized and there is insufficient space
     * available to write into it, the EventFlag bit mask 'readNotification' is
     * is waited upon.
     *
     * Upon a successful write, wake is called on 'writeNotification' (if
     * non-zero).
     *
     * @param data Pointer to the array of items of type T.
     * @param count Number of items in array.
     * @param readNotification The EventFlag bit mask to wait on if there is not
     * enough space in FMQ to write 'count' items.
     * @param writeNotification The EventFlag bit mask to call wake on
     * a successful write. No wake is called if 'writeNotification' is zero.
     * @param timeOutNanos Number of nanoseconds after which the blocking
     * write attempt is aborted.
     * @param evFlag The EventFlag object to be used for blocking. If nullptr,
     * it is checked whether the FMQ owns an EventFlag object and that is used
     * for blocking instead.
     *
     * @return Whether the write was successful.
     */

    bool writeBlocking(const T* data, size_t count, uint32_t readNotification,
                       uint32_t writeNotification, int64_t timeOutNanos = 0,
                       android::hardware::EventFlag* evFlag = nullptr);

    /**
     * Read some data from the FMQ without blocking.
     *
     * @param data Pointer to the array to which read data is to be written.
     * @param count Number of items to be read.
     *
     * @return Whether the read was successful.
     */
    bool read(T* data, size_t count);

    /**
     * Perform a blocking read operation of 'count' items from the FMQ. Does not
     * perform a partial read.
     *
     * If 'evFlag' is nullptr, it is checked whether there is an EventFlag object
     * associated with the FMQ and it is used in that case.
     *
     * The application code must ensure that 'evFlag' used by the
     * reader(s)/writer is based upon the same EventFlag word.
     *
     * The method will return false without blocking if any of the following
     * conditions are true:
     * -If 'evFlag' is nullptr and the FMQ does not own an EventFlag object.
     * -If the 'writeNotification' bit mask is zero.
     * -If 'count' is greater than the FMQ size.
     *
     * If FMQ does not contain 'count' items, the eventFlag bit mask
     * 'writeNotification' is waited upon. Upon a successful read from the FMQ,
     * wake is called on 'readNotification' (if non-zero).
     *
     * @param data Pointer to the array to which read data is to be written.
     * @param count Number of items to be read.
     * @param readNotification The EventFlag bit mask to call wake on after
     * a successful read. No wake is called if 'readNotification' is zero.
     * @param writeNotification The EventFlag bit mask to call a wait on
     * if there is insufficient data in the FMQ to be read.
     * @param timeOutNanos Number of nanoseconds after which the blocking
     * read attempt is aborted.
     * @param evFlag The EventFlag object to be used for blocking.
     *
     * @return Whether the read was successful.
     */
    bool readBlocking(T* data, size_t count, uint32_t readNotification,
                      uint32_t writeNotification, int64_t timeOutNanos = 0,
                      android::hardware::EventFlag* evFlag = nullptr);

    /**
     * Get a pointer to the MQDescriptor object that describes this FMQ.
     *
     * @return Pointer to the MQDescriptor associated with the FMQ.
     */
    const MQDescriptor<T, flavor>* getDesc() const { return mDesc.get(); }

    /**
     * Get a pointer to the EventFlag word if there is one associated with this FMQ.
     *
     * @return Pointer to an EventFlag word, will return nullptr if not
     * configured. This method does not transfer ownership. The EventFlag
     * word will be unmapped by the MessageQueue destructor.
     */
    std::atomic<uint32_t>* getEventFlagWord() const { return mEvFlagWord; }
private:
    struct region {
        uint8_t* address;
        size_t length;
    };
    struct transaction {
        region first;
        region second;
    };

    size_t writeBytes(const uint8_t* data, size_t size);
    transaction beginWrite(size_t nBytesDesired) const;
    void commitWrite(size_t nBytesWritten);

    size_t readBytes(uint8_t* data, size_t size);
    transaction beginRead(size_t nBytesDesired) const;
    void commitRead(size_t nBytesRead);

    size_t availableToWriteBytes() const;
    size_t availableToReadBytes() const;

    MessageQueue(const MessageQueue& other) = delete;
    MessageQueue& operator=(const MessageQueue& other) = delete;
    MessageQueue();

    void* mapGrantorDescr(uint32_t grantorIdx);
    void unmapGrantorDescr(void* address, uint32_t grantorIdx);
    void initMemory(bool resetPointers);

    std::unique_ptr<MQDescriptor<T, flavor>> mDesc;
    uint8_t* mRing = nullptr;
    /*
     * TODO(b/31550092): Change to 32 bit read and write pointer counters.
     */
    std::atomic<uint64_t>* mReadPtr = nullptr;
    std::atomic<uint64_t>* mWritePtr = nullptr;

    std::atomic<uint32_t>* mEvFlagWord = nullptr;

    /*
     * This EventFlag object will be owned by the FMQ and will have the same
     * lifetime.
     */
    android::hardware::EventFlag* mEventFlag = nullptr;
};

template <typename T, MQFlavor flavor>
void MessageQueue<T, flavor>::initMemory(bool resetPointers) {
    /*
     * Verify that the the Descriptor contains the minimum number of grantors
     * the native_handle is valid and T matches quantum size.
     */
    if ((mDesc == nullptr) || !mDesc->isHandleValid() ||
        (mDesc->countGrantors() < MQDescriptor<T, flavor>::kMinGrantorCount) ||
        (mDesc->getQuantum() != sizeof(T))) {
        return;
    }

    if (flavor == kSynchronizedReadWrite) {
        mReadPtr =
                reinterpret_cast<std::atomic<uint64_t>*>
                (mapGrantorDescr(MQDescriptor<T, flavor>::READPTRPOS));
    } else {
        /*
         * The unsynchronized write flavor of the FMQ may have multiple readers
         * and each reader would have their own read pointer counter.
         */
        mReadPtr = new (std::nothrow) std::atomic<uint64_t>;
    }

    CHECK(mReadPtr != nullptr);

    mWritePtr =
            reinterpret_cast<std::atomic<uint64_t>*>
            (mapGrantorDescr(MQDescriptor<T, flavor>::WRITEPTRPOS));
    CHECK(mWritePtr != nullptr);

    if (resetPointers) {
        mReadPtr->store(0, std::memory_order_release);
        mWritePtr->store(0, std::memory_order_release);
    } else if (flavor != kSynchronizedReadWrite) {
        // Always reset the read pointer.
        mReadPtr->store(0, std::memory_order_release);
    }

    mRing = reinterpret_cast<uint8_t*>(mapGrantorDescr
                                       (MQDescriptor<T, flavor>::DATAPTRPOS));
    CHECK(mRing != nullptr);

    mEvFlagWord = static_cast<std::atomic<uint32_t>*>(
      mapGrantorDescr(MQDescriptor<T, flavor>::EVFLAGWORDPOS));
    if (mEvFlagWord != nullptr) {
        android::hardware::EventFlag::createEventFlag(mEvFlagWord, &mEventFlag);
    }
}

template <typename T, MQFlavor flavor>
MessageQueue<T, flavor>::MessageQueue(const MQDescriptor<T, flavor>& Desc, bool resetPointers) {
    mDesc = std::unique_ptr<MQDescriptor<T, flavor>>(new (std::nothrow) MQDescriptor<T, flavor>(Desc));
    if (mDesc == nullptr) {
        return;
    }

    initMemory(resetPointers);
}

template <typename T, MQFlavor flavor>
MessageQueue<T, flavor>::MessageQueue(size_t numElementsInQueue, bool configureEventFlagWord) {
    /*
     * The FMQ needs to allocate memory for the ringbuffer as well as for the
     * read and write pointer counters. If an EventFlag word is to be configured,
     * we also need to allocate memory for the same/
     */
    size_t kQueueSizeBytes = numElementsInQueue * sizeof(T);
    size_t kMetaDataSize = 2 * sizeof(android::hardware::RingBufferPosition);

    if (configureEventFlagWord) {
        kMetaDataSize+= sizeof(std::atomic<uint32_t>);
    }

    /*
     * Ashmem memory region size needs to
     * be specified in page-aligned bytes.
     */
    size_t kAshmemSizePageAligned =
            (kQueueSizeBytes + kMetaDataSize + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    /*
     * Create an ashmem region to map the memory for the ringbuffer,
     * read counter and write counter.
     */
    int ashmemFd = ashmem_create_region("MessageQueue", kAshmemSizePageAligned);
    ashmem_set_prot_region(ashmemFd, PROT_READ | PROT_WRITE);

    /*
     * The native handle will contain the fds to be mapped.
     */
    native_handle_t* mqHandle =
            native_handle_create(1 /* numFds */, 0 /* numInts */);
    if (mqHandle == nullptr) {
        return;
    }

    mqHandle->data[0] = ashmemFd;
    mDesc = std::unique_ptr<MQDescriptor<T, flavor>>(
            new (std::nothrow) MQDescriptor<T, flavor>(kQueueSizeBytes,
                                                    mqHandle,
                                                    sizeof(T),
                                                    configureEventFlagWord));
    if (mDesc == nullptr) {
        return;
    }
    initMemory(true);
}

template <typename T, MQFlavor flavor>
MessageQueue<T, flavor>::~MessageQueue() {
    if (flavor == kUnsynchronizedWrite) {
        delete mReadPtr;
    } else {
        unmapGrantorDescr(mReadPtr, MQDescriptor<T, flavor>::READPTRPOS);
    }
    if (mWritePtr != nullptr) {
        unmapGrantorDescr(mWritePtr,
                                     MQDescriptor<T, flavor>::WRITEPTRPOS);
    }
    if (mRing != nullptr) {
        unmapGrantorDescr(mRing, MQDescriptor<T, flavor>::DATAPTRPOS);
    }
    if (mEvFlagWord != nullptr) {
        unmapGrantorDescr(mEvFlagWord, MQDescriptor<T, flavor>::EVFLAGWORDPOS);
        android::hardware::EventFlag::deleteEventFlag(&mEventFlag);
    }
}

template <typename T, MQFlavor flavor>
bool MessageQueue<T, flavor>::write(const T* data) {
    return write(data, 1);
}

template <typename T, MQFlavor flavor>
bool MessageQueue<T, flavor>::read(T* data) {
    return read(data, 1);
}

template <typename T, MQFlavor flavor>
bool MessageQueue<T, flavor>::write(const T* data, size_t count) {
    /*
     * If read/write synchronization is not enabled, data in the queue
     * will be overwritten by a write operation when full.
     */
    if ((flavor == kSynchronizedReadWrite && (availableToWriteBytes() < sizeof(T) * count)) ||
        (count > getQuantumCount()))
        return false;

    return (writeBytes(reinterpret_cast<const uint8_t*>(data),
                       sizeof(T) * count) == sizeof(T) * count);
}

template <typename T, MQFlavor flavor>
bool MessageQueue<T, flavor>::writeBlocking(const T* data,
                                            size_t count,
                                            uint32_t readNotification,
                                            uint32_t writeNotification,
                                            int64_t timeOutNanos,
                                            android::hardware::EventFlag* evFlag) {
    /*
     * If evFlag is null and the FMQ does not have its own EventFlag object
     * return false;
     * If the flavor is kSynchronizedReadWrite and the readNotification
     * bit mask is zero return false;
     * If the count is greater than queue size, return false
     * to prevent blocking until timeOut.
     */
    if (evFlag == nullptr) {
        evFlag = mEventFlag;
        if (evFlag == nullptr) {
            return false;
        }
    }

    if ((readNotification == 0 && flavor == kSynchronizedReadWrite) ||
        (count > getQuantumCount())) {
        return false;
    }

    /*
     * There is no need to wait for a readNotification if the flavor
     * of the queue is kUnsynchronizedWrite or sufficient space to write
     * is already present in the FMQ. The latter would be the case when
     * read operations read more number of messages than
     * write operations write. In other words, a single large read may clear the FMQ
     * after multiple small writes. This would fail to clear a pending
     * readNotification bit since EventFlag bits can only be cleared
     * by a wait() call, however the bit would be correctly cleared by the next
     * blockingWrite() call.
     */

    bool result = write(data, count);
    if (result) {
        if (writeNotification) {
            evFlag->wake(writeNotification);
        }
        return result;
    }

    bool endWait = false;
    while (endWait == false) {
        uint32_t efState = 0;
        /*
         * wait() will return immediately if there was a pending read
         * notification.
         */
        status_t status = evFlag->wait(readNotification, &efState, timeOutNanos);
        switch(status) {
            case android::NO_ERROR:
                /*
                 * If wait() returns NO_ERROR, break and check efState.
                 */
                break;
            case android::TIMED_OUT:
                /*
                 * If wait() returns android::TIMEDOUT, break out of the while loop
                 * and return false;
                 */
                endWait = true;
                continue;
            case -EAGAIN:
            case -EINTR:
                /*
                 * For errors -EAGAIN and -EINTR, go back to wait.
                 */
                continue;
            default:
                /*
                 * Throw an error for any other error code since it is unexpected.
                 */

                endWait = true;
                ALOGE("Unexpected error code from EventFlag Wait %d", status);
                continue;
        }

        /*
         * If the wake() was not due to the readNotification bit or if
         * there is still insufficient space to write to the FMQ,
         * keep waiting for another readNotification.
         */
        if ((efState & readNotification) && write(data, count)) {
            if (writeNotification) {
                evFlag->wake(writeNotification);
            }
            result = true;
            endWait = true;
        }
    }

    return result;
}

template <typename T, MQFlavor flavor>
bool MessageQueue<T, flavor>::readBlocking(T* data,
                                           size_t count,
                                           uint32_t readNotification,
                                           uint32_t writeNotification,
                                           int64_t timeOutNanos,
                                           android::hardware::EventFlag* evFlag) {
    /*
     * If evFlag is null and the FMQ does not own its own EventFlag object
     * return false;
     * If the writeNotification bit mask is zero return false;
     * If the count is greater than queue size, return false to prevent
     * blocking until timeOut.
     */
    if (evFlag == nullptr) {
        evFlag = mEventFlag;
        if (evFlag == nullptr) {
            return false;
        }
    }

    if (writeNotification == 0 || count > getQuantumCount()) {
        return false;
    }

    /*
     * There is no need to wait for a write notification if sufficient
     * data to read is already present in the FMQ. This would be the
     * case when read operations read lesser number of messages than
     * a write operation and multiple reads would be required to clear the queue
     * after a single write operation. This check would fail to clear a pending
     * writeNotification bit since EventFlag bits can only be cleared
     * by a wait() call, however the bit would be correctly cleared by the next
     * readBlocking() call.
     */

    bool result = read(data, count);
    if (result) {
        if (readNotification) {
            evFlag->wake(readNotification);
        }
        return result;
    }

    bool endWait = false;
    while (endWait == false) {
        uint32_t efState = 0;
        /*
         * wait() will return immediately if there was a pending write
         * notification.
         */
        status_t status = evFlag->wait(writeNotification, &efState, timeOutNanos);
        switch(status) {
            case android::NO_ERROR:
                /*
                 * If wait() returns NO_ERROR, break and check efState.
                 */
                break;
            case android::TIMED_OUT:
                /*
                 * If wait() returns android::TIMEDOUT, break out of the while loop
                 * and return false;
                 */
                endWait = true;
                continue;
            case -EAGAIN:
            case -EINTR:
                /*
                 * For errors -EAGAIN and -EINTR, go back to wait.
                 */
                continue;
            default:
                /*
                 * Throw an error for any other error code since it is unexpected.
                 */

                endWait = true;
                ALOGE("Unexpected error code from EventFlag Wait %d", status);
                continue;
        }

        /*
         * If the wake() was not due to the writeNotification bit being set
         * or if the data in FMQ is still insufficient, go back to waiting
         * for another write notification.
         */
        if ((efState & writeNotification) && read(data, count)) {
            if (readNotification) {
                evFlag->wake(readNotification);
            }
            result = true;
            endWait = true;
        }
    }

    return result;
}

template <typename T, MQFlavor flavor>
__attribute__((no_sanitize("integer")))
bool MessageQueue<T, flavor>::read(T* data, size_t count) {
    if (availableToReadBytes() < sizeof(T) * count) return false;
    /*
     * If it is detected that the data in the queue was overwritten
     * due to the reader process being too slow, the read pointer counter
     * is set to the same as the write pointer counter to indicate error
     * and the read returns false;
     * Need acquire/release memory ordering for mWritePtr.
     */
    auto writePtr = mWritePtr->load(std::memory_order_acquire);
    /*
     * A relaxed load is sufficient for mReadPtr since there will be no
     * stores to mReadPtr from a different thread.
     */
    auto readPtr = mReadPtr->load(std::memory_order_relaxed);

    if (writePtr - readPtr > mDesc->getSize()) {
        mReadPtr->store(writePtr, std::memory_order_release);
        return false;
    }

    return readBytes(reinterpret_cast<uint8_t*>(data), sizeof(T) * count) ==
            sizeof(T) * count;
}

template <typename T, MQFlavor flavor>
size_t MessageQueue<T, flavor>::availableToWriteBytes() const {
    return mDesc->getSize() - availableToReadBytes();
}

template <typename T, MQFlavor flavor>
size_t MessageQueue<T, flavor>::availableToWrite() const {
    return availableToWriteBytes()/sizeof(T);
}

template <typename T, MQFlavor flavor>
size_t MessageQueue<T, flavor>::availableToRead() const {
    return availableToReadBytes()/sizeof(T);
}

template <typename T, MQFlavor flavor>
size_t MessageQueue<T, flavor>::writeBytes(const uint8_t* data, size_t size) {
    transaction tx = beginWrite(size);
    memcpy(tx.first.address, data, tx.first.length);
    memcpy(tx.second.address, data + tx.first.length, tx.second.length);
    size_t result = tx.first.length + tx.second.length;
    commitWrite(result);
    return result;
}

/*
 * The below method does not check for available space since it was already
 * checked by write() API which invokes writeBytes() which in turn calls
 * beginWrite().
 */
template <typename T, MQFlavor flavor>
typename MessageQueue<T, flavor>::transaction MessageQueue<T, flavor>::beginWrite(
        size_t nBytesDesired) const {
    transaction result;
    auto writePtr = mWritePtr->load(std::memory_order_relaxed);
    size_t writeOffset = writePtr % mDesc->getSize();
    size_t contiguous = mDesc->getSize() - writeOffset;
    if (contiguous < nBytesDesired) {
        result = {{mRing + writeOffset, contiguous},
            {mRing, nBytesDesired - contiguous}};
    } else {
        result = {
            {mRing + writeOffset, nBytesDesired}, {0, 0},
        };
    }
    return result;
}

template <typename T, MQFlavor flavor>
__attribute__((no_sanitize("integer")))
void MessageQueue<T, flavor>::commitWrite(size_t nBytesWritten) {
    auto writePtr = mWritePtr->load(std::memory_order_relaxed);
    writePtr += nBytesWritten;
    mWritePtr->store(writePtr, std::memory_order_release);
}

template <typename T, MQFlavor flavor>
size_t MessageQueue<T, flavor>::availableToReadBytes() const {
    /*
     * This method is invoked by implementations of both read() and write() and
     * hence requries a memory_order_acquired load for both mReadPtr and
     * mWritePtr.
     */
    return mWritePtr->load(std::memory_order_acquire) -
            mReadPtr->load(std::memory_order_acquire);
}

template <typename T, MQFlavor flavor>
size_t MessageQueue<T, flavor>::readBytes(uint8_t* data, size_t size) {
    transaction tx = beginRead(size);
    memcpy(data, tx.first.address, tx.first.length);
    memcpy(data + tx.first.length, tx.second.address, tx.second.length);
    size_t result = tx.first.length + tx.second.length;
    commitRead(result);
    return result;
}

/*
 * The below method does not check whether nBytesDesired bytes are available
 * to read because the check is performed in the read() method before
 * readBytes() is invoked.
 */
template <typename T, MQFlavor flavor>
typename MessageQueue<T, flavor>::transaction MessageQueue<T, flavor>::beginRead(
        size_t nBytesDesired) const {
    transaction result;
    auto readPtr = mReadPtr->load(std::memory_order_relaxed);
    size_t readOffset = readPtr % mDesc->getSize();
    size_t contiguous = mDesc->getSize() - readOffset;

    if (contiguous < nBytesDesired) {
        result = {{mRing + readOffset, contiguous},
            {mRing, nBytesDesired - contiguous}};
    } else {
        result = {
            {mRing + readOffset, nBytesDesired}, {0, 0},
        };
    }

    return result;
}

template <typename T, MQFlavor flavor>
__attribute__((no_sanitize("integer")))
void MessageQueue<T, flavor>::commitRead(size_t nBytesRead) {
    auto readPtr = mReadPtr->load(std::memory_order_relaxed);
    readPtr += nBytesRead;
    mReadPtr->store(readPtr, std::memory_order_release);
}

template <typename T, MQFlavor flavor>
size_t MessageQueue<T, flavor>::getQuantumSize() const {
    return mDesc->getQuantum();
}

template <typename T, MQFlavor flavor>
size_t MessageQueue<T, flavor>::getQuantumCount() const {
    return mDesc->getSize() / mDesc->getQuantum();
}

template <typename T, MQFlavor flavor>
bool MessageQueue<T, flavor>::isValid() const {
    return mRing != nullptr && mReadPtr != nullptr && mWritePtr != nullptr;
}

template <typename T, MQFlavor flavor>
void* MessageQueue<T, flavor>::mapGrantorDescr(uint32_t grantorIdx) {
    const native_handle_t* handle = mDesc->getNativeHandle()->handle();
    auto mGrantors = mDesc->getGrantors();
    if ((handle == nullptr) || (grantorIdx >= mGrantors.size())) {
        return nullptr;
    }

    int fdIndex = mGrantors[grantorIdx].fdIndex;
    /*
     * Offset for mmap must be a multiple of PAGE_SIZE.
     */
    int mapOffset = (mGrantors[grantorIdx].offset / PAGE_SIZE) * PAGE_SIZE;
    int mapLength =
            mGrantors[grantorIdx].offset - mapOffset + mGrantors[grantorIdx].extent;

    void* address = mmap(0, mapLength, PROT_READ | PROT_WRITE, MAP_SHARED,
                         handle->data[fdIndex], mapOffset);
    return (address == MAP_FAILED)
            ? nullptr
            : reinterpret_cast<uint8_t*>(address) +
            (mGrantors[grantorIdx].offset - mapOffset);
}

template <typename T, MQFlavor flavor>
void MessageQueue<T, flavor>::unmapGrantorDescr(void* address,
                                                uint32_t grantorIdx) {
    auto mGrantors = mDesc->getGrantors();
    if ((address == nullptr) || (grantorIdx >= mGrantors.size())) {
        return;
    }

    int mapOffset = (mGrantors[grantorIdx].offset / PAGE_SIZE) * PAGE_SIZE;
    int mapLength =
            mGrantors[grantorIdx].offset - mapOffset + mGrantors[grantorIdx].extent;
    void* baseAddress = reinterpret_cast<uint8_t*>(address) -
            (mGrantors[grantorIdx].offset - mapOffset);
    if (baseAddress) munmap(baseAddress, mapLength);
}

}  // namespace hardware
}  // namespace android
#endif  // HIDL_MQ_H
