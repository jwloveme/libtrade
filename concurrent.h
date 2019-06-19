//
// Created by kyl on 2019-06-19.
//

#ifndef LIBTRADE_CONCURRENT_H
#define LIBTRADE_CONCURRENT_H

#include <boost/noncopyable.hpp>

#if !defined(_WIN32) && !defined(_WIN64)
#else
#if defined(_WIN32) ||  defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif
#include <winsock2.h>
#include <Windows.h>
#endif

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>
#if !defined(_WIN32) && !defined(_WIN64)
#include <folly/RWSpinLock.h>
#endif

#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/interprocess_condition.hpp>

#if defined(_WIN32) || defined(_WIN32)

#if defined(__GNUC__) && __GNUC__ >= 4
#define LIKELY(x)   (__builtin_expect((x), 1))
#define UNLIKELY(x) (__builtin_expect((x), 0))
#else
#define LIKELY(x)   (x)
#define UNLIKELY(x) (x)
#endif

namespace folly
{
class RWSpinLock : boost::noncopyable
{
enum : int32_t { READER = 4, UPGRADED = 2, WRITER = 1 };
public:
RWSpinLock() : bits_(0) {}

// Lockable Concept
void lock()
{
    /*    int count = 0;
    while (!LIKELY(try_lock())) {
    if (++count > 1000) YieldProcessor();
    }*/
}

// Writer is responsible for clearing up both the UPGRADED and WRITER bits.
void unlock()
{
    /*        static_assert(READER > WRITER + UPGRADED, "wrong bits!");
    bits_.fetch_and(~(WRITER | UPGRADED), std::memory_order_release);*/
}

// SharedLockable Concept
void lock_shared()
{
    int count = 0;
    while (!LIKELY(try_lock_shared()))
    {
        if (++count > 1000) YieldProcessor();
    }
}

void unlock_shared()
{
    bits_.fetch_add(-READER, std::memory_order_release);
}

// Downgrade the lock from writer status to reader status.
void unlock_and_lock_shared()
{
    bits_.fetch_add(READER, std::memory_order_acquire);
    unlock();
}

// UpgradeLockable Concept
void lock_upgrade()
{
    int count = 0;
    while (!try_lock_upgrade())
    {
        if (++count > 1000) YieldProcessor();
    }
}

void unlock_upgrade()
{
    bits_.fetch_add(-UPGRADED, std::memory_order_acq_rel);
}

// unlock upgrade and try to acquire write lock
void unlock_upgrade_and_lock()
{
    int64_t count = 0;
    while (!try_unlock_upgrade_and_lock())
    {
        if (++count > 1000) YieldProcessor();
    }
}

// unlock upgrade and read lock atomically
void unlock_upgrade_and_lock_shared()
{
    bits_.fetch_add(READER - UPGRADED, std::memory_order_acq_rel);
}

// write unlock and upgrade lock atomically
void unlock_and_lock_upgrade()
{
    // need to do it in two steps here -- as the UPGRADED bit might be OR-ed at
    // the same time when other threads are trying do try_lock_upgrade().
    bits_.fetch_or(UPGRADED, std::memory_order_acquire);
    bits_.fetch_add(-WRITER, std::memory_order_release);
}


// Attempt to acquire writer permission. Return false if we didn't get it.
bool try_lock()
{
    int32_t expect = 0;
    return bits_.compare_exchange_strong(expect, WRITER,
                                         std::memory_order_acq_rel);
}

// Try to get reader permission on the lock. This can fail if we
// find out someone is a writer or upgrader.
// Setting the UPGRADED bit would allow a writer-to-be to indicate
// its intention to write and block any new readers while waiting
// for existing readers to finish and release their read locks. This
// helps avoid starving writers (promoted from upgraders).
bool try_lock_shared()
{
    // fetch_add is considerably (100%) faster than compare_exchange,
    // so here we are optimizing for the common (lock success) case.
    int32_t value = bits_.fetch_add(READER, std::memory_order_acquire);
    if (UNLIKELY(value & (WRITER | UPGRADED)))
    {
        bits_.fetch_add(-READER, std::memory_order_release);
        return false;
    }
    return true;
}

// try to unlock upgrade and write lock atomically
bool try_unlock_upgrade_and_lock()
{
    int32_t expect = UPGRADED;
    return bits_.compare_exchange_strong(expect, WRITER,
                                         std::memory_order_acq_rel);
}

// try to acquire an upgradable lock.
bool try_lock_upgrade()
{
    int32_t value = bits_.fetch_or(UPGRADED, std::memory_order_acquire);

    // Note: when failed, we cannot flip the UPGRADED bit back,
    // as in this case there is either another upgrade lock or a write lock.
    // If it's a write lock, the bit will get cleared up when that lock's done
    // with unlock().
    return ((value & (UPGRADED | WRITER)) == 0);
}

// mainly for debugging purposes.
int32_t bits() const
{
    return bits_.load(std::memory_order_acquire);
}

class ReadHolder;
class UpgradedHolder;
class WriteHolder;

class ReadHolder
{
public:
    explicit ReadHolder(RWSpinLock* lock = nullptr) : lock_(lock)
    {
        if (lock_) lock_->lock_shared();
    }

    explicit ReadHolder(RWSpinLock& lock) : lock_(&lock)
    {
        lock_->lock_shared();
    }

    ReadHolder(ReadHolder&& other) : lock_(other.lock_)
    {
        other.lock_ = nullptr;
    }

    // down-grade
    explicit ReadHolder(UpgradedHolder&& upgraded) : lock_(upgraded.lock_)
    {
        upgraded.lock_ = nullptr;
        if (lock_) lock_->unlock_upgrade_and_lock_shared();
    }

    explicit ReadHolder(WriteHolder&& writer) : lock_(writer.lock_)
    {
        writer.lock_ = nullptr;
        if (lock_) lock_->unlock_and_lock_shared();
    }

    ReadHolder& operator= (ReadHolder&& other)
    {
        using std::swap;
        swap(lock_, other.lock_);
        return *this;
    }

    ReadHolder(const ReadHolder& other) = delete;
    ReadHolder& operator= (const ReadHolder& other) = delete;

    ~ReadHolder()
    {
        if (lock_) lock_->unlock_shared();
    }

    void reset(RWSpinLock* lock = nullptr)
    {
        if (lock == lock_) return;
        if (lock_) lock_->unlock_shared();
        lock_ = lock;
        if (lock_) lock_->lock_shared();
    }

    void swap(ReadHolder* other)
    {
        std::swap(lock_, other->lock_);
    }

private:
    friend class UpgradedHolder;
    friend class WriteHolder;
    RWSpinLock* lock_;
};

class UpgradedHolder
{
public:
    explicit UpgradedHolder(RWSpinLock* lock = nullptr) : lock_(lock)
    {
        if (lock_) lock_->lock_upgrade();
    }

    explicit UpgradedHolder(RWSpinLock& lock) : lock_(&lock)
    {
        lock_->lock_upgrade();
    }

    explicit UpgradedHolder(WriteHolder&& writer)
    {
        lock_ = writer.lock_;
        writer.lock_ = nullptr;
        if (lock_) lock_->unlock_and_lock_upgrade();
    }

    UpgradedHolder(UpgradedHolder&& other) : lock_(other.lock_)
    {
        other.lock_ = nullptr;
    }

    UpgradedHolder& operator = (UpgradedHolder&& other)
    {
        using std::swap;
        swap(lock_, other.lock_);
        return *this;
    }

    UpgradedHolder(const UpgradedHolder& other) = delete;
    UpgradedHolder& operator = (const UpgradedHolder& other) = delete;

    ~UpgradedHolder()
    {
        if (lock_) lock_->unlock_upgrade();
    }

    void reset(RWSpinLock* lock = nullptr)
    {
        if (lock == lock_) return;
        if (lock_) lock_->unlock_upgrade();
        lock_ = lock;
        if (lock_) lock_->lock_upgrade();
    }

    void swap(UpgradedHolder* other)
    {
        using std::swap;
        swap(lock_, other->lock_);
    }

private:
    friend class WriteHolder;
    friend class ReadHolder;
    RWSpinLock* lock_;
};

class WriteHolder
{
public:
    explicit WriteHolder(RWSpinLock* lock = nullptr) : lock_(lock)
    {
        if (lock_) lock_->lock();
    }

    explicit WriteHolder(RWSpinLock& lock) : lock_(&lock)
    {
        lock_->lock();
    }

    // promoted from an upgrade lock holder
    explicit WriteHolder(UpgradedHolder&& upgraded)
    {
        lock_ = upgraded.lock_;
        upgraded.lock_ = nullptr;
        if (lock_) lock_->unlock_upgrade_and_lock();
    }

    WriteHolder(WriteHolder&& other) : lock_(other.lock_)
    {
        other.lock_ = nullptr;
    }

    WriteHolder& operator = (WriteHolder&& other)
    {
        using std::swap;
        swap(lock_, other.lock_);
        return *this;
    }

    WriteHolder(const WriteHolder& other) = delete;
    WriteHolder& operator = (const WriteHolder& other) = delete;

    ~WriteHolder()
    {
        if (lock_) lock_->unlock();
    }

    void reset(RWSpinLock* lock = nullptr)
    {
        if (lock == lock_) return;
        if (lock_) lock_->unlock();
        lock_ = lock;
        if (lock_) lock_->lock();
    }

    void swap(WriteHolder* other)
    {
        using std::swap;
        swap(lock_, other->lock_);
    }

private:
    friend class ReadHolder;
    friend class UpgradedHolder;
    RWSpinLock* lock_;
};

// Synchronized<> adaptors
friend void acquireRead(RWSpinLock& l)
{
    return l.lock_shared();
}
friend void acquireReadWrite(RWSpinLock& l)
{
    return l.lock();
}
friend void releaseRead(RWSpinLock& l)
{
    return l.unlock_shared();
}
friend void releaseReadWrite(RWSpinLock& l)
{
    return l.unlock();
}

private:
    std::atomic<int32_t> bits_;
};

} // namespace folly


namespace trade
{
namespace concurrent
{
bool WaitUntil ( std::function<bool() > predicate, int sleep, int timeout );

void Sleep(int sec);

// void DelayRun(std::function<void (  ) > action, int seconds);

class AutoResetEvent
{
public:
    explicit AutoResetEvent ( bool initial = false );

    void Set();
    void Reset();
    bool WaitOne();
    bool WaitOne ( int interval );
private:
    AutoResetEvent ( const AutoResetEvent& );
    AutoResetEvent& operator= ( const AutoResetEvent& ); // non-copyable
    bool flag_;
    std::mutex protect_;
    std::condition_variable signal_;
};

class SharedAutoResetEvent
{
public:
    explicit SharedAutoResetEvent ( bool initial = false );

    void Set();
    void Reset();

    bool WaitOne();

    bool WaitOne ( int interval );

private:
    SharedAutoResetEvent ( const SharedAutoResetEvent& );
    SharedAutoResetEvent& operator= ( const SharedAutoResetEvent& ); // non-copyable
    bool flag_;
    boost::interprocess::interprocess_condition signal_;
    boost::interprocess::interprocess_mutex protect_;
};

struct Locker
{
    Locker ( folly::RWSpinLock& l ) : locker ( l )
    {
        locker.lock();
    }
    Locker ( const folly::RWSpinLock& l ) : locker ( const_cast<folly::RWSpinLock&> ( l ) )
    {
        locker.lock();
    }
    ~Locker()
    {
        locker.unlock();
    }

private:
    folly::RWSpinLock& locker;
};

struct TryLocker
{
    TryLocker ( folly::RWSpinLock& l ) : locker ( l )
    {
        success = locker.try_lock();
    }
    TryLocker ( const folly::RWSpinLock& l ) : locker ( const_cast<folly::RWSpinLock&> ( l ) )
    {
        success = locker.try_lock();
    }
    ~TryLocker()
    {
        if ( success )
        {
            locker.unlock();
        }
    }

    inline bool Success() const
    {
        return success;
    }

private:
    folly::RWSpinLock& locker;
    bool success = true;
};

struct SharedLocker
{
    SharedLocker ( folly::RWSpinLock& l ) : locker ( l )
    {
        locker.lock_shared();
    }
    SharedLocker ( const folly::RWSpinLock& l ) : locker ( const_cast<folly::RWSpinLock&> ( l ) )
    {
        locker.lock_shared();
    }
    ~SharedLocker()
    {
        locker.unlock_shared();
    }

private:
    folly::RWSpinLock& locker;
};

struct TrySharedLocker
{
    TrySharedLocker ( folly::RWSpinLock& l ) : locker ( l )
    {
        success = locker.try_lock_shared();
    }
    TrySharedLocker ( const folly::RWSpinLock& l ) : locker ( const_cast<folly::RWSpinLock&> ( l ) )
    {
        success = locker.try_lock_shared();
    }
    ~TrySharedLocker()
    {
        if ( success )
        {
            locker.unlock_shared();
        }
    }

    inline bool Success() const
    {
        return success;
    }

private:
    folly::RWSpinLock& locker;
    bool success = true;
};

// obsolete to reader/writer in folly from facebook
#if !defined(_WIN32) && !defined(_WIN32)
class SpinLock
{
//public:
//    SpinLock(std::atomic_flag l):lock(l)
//    {
//
//        while (lock.test_and_set(std::memory_order_acquire))  // acquire lock
//            std::this_thread::sleep_for(0); ; // spin
//    }
//
//    ~Spinlock(){  lock.clear(std::memory_order_release);  }             // release lock
//private:
//    std::atomic_flag lock = ATOMIC_FLAG_INIT;
};

class RWSpinLock
{
};
#endif
}
}





#endif //LIBTRADE_CONCURRENT_H
