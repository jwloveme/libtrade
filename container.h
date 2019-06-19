//
// Created by kyl on 2019-06-19.
//

#ifndef LIBTRADE_CONTAINER_H
#define LIBTRADE_CONTAINER_H

#include <array>
#include <atomic>
#include <functional>

#if !defined(_WIN32) && !defined(_WIN64)
#include <folly/RWSpinLock.h>
#endif

namespace trade
{
namespace container
{
template<class T, int N>
class FixedArray
{
public:
    inline T& at(int pos)
    {
        return data[pos];
    }

    inline T& operator [](int pos)
    {
        return data[pos];
    }

    inline int size() const
    {
        return N;
    }

private:
    T data[N];
};

template<class T, int N>
class SyncList
{
public:
    SyncList()
    {
    }

        inline void Enqueue ( const T& t )
        {
            using namespace natural_threading;
            Locker lk ( locker );
            buffer.push_back ( t );
        }

        template<class TFunc>
        inline void Emplace ( const TFunc& action )
        {
            using namespace natural_threading;
            Locker lk ( locker );
            auto it = buffer.emplace ( buffer.end() );
            action ( *it );
        }

        inline long GetLatestEntryIndex() const
        {
            return buffer.size() - 1;
        }

        inline long GetEndIndex() const
        {
            return buffer.size();
        }

        inline const T& GetLatestEntryToRead() const
        {
            return this->operator[] ( GetLatestEntryIndex() );
        }

        inline const T& operator [] ( long i ) const
        {
            return i < 0? t: buffer[i ];
        }

        template<class TFunc>
        inline long Dequeue ( long cursor, const TFunc& action )
        {
            using namespace natural_threading;
            //assume N is big enough and action is fast enough, so that element will not be overwrite after pop up;
            while ( true )
            {
                T* t = nullptr;
                {
                    if ( cursor < buffer.size() )
                    {
                        t = & ( buffer[cursor] );
                        cursor ++;
                    }
                    else
                    {
                        break;
                    }
                }
                action ( *t );
            }
            return cursor;
        }

        inline int Size() const
        {
            return buffer.size();
        }

        inline void Clear()
        {
            buffer.clear();
        }

        inline long Capacity() const
        {
            return buffer.capacity();
        }

    private:
        std::vector<T> buffer;
        T t;
        folly::RWSpinLock locker;
    };

    template<class T, int N>
    class SyncRingBuffer
    {
    public:
        SyncRingBuffer()
        {
        }

        static_assert ( ( ( N > 0 ) && ( ( N & ( ~N + 1 ) ) == N ) ),
                        "SyncRingBuffer's size must be a positive power of 2" );

        inline void Enqueue ( const T& t )
        {
            using namespace natural_threading;
            Locker lk ( headlocker );
            buffer[head & mask] = t;
            head++;
        }

        template<class TFunc>
        inline void Emplace ( const TFunc& action )
        {
            using namespace natural_threading;
            Locker lk ( headlocker );
            action ( buffer[head & mask] );
            head++;
        }

        inline long GetLatestEntryIndex() const
        {
            return head - 1;
        }

        inline long GetEndIndex() const
        {
            return head;
        }

        inline const T& GetLatestEntryToRead() const
        {
            return this->operator[] ( GetLatestEntryIndex() );
        }

        inline const T& operator [] ( long i ) const
        {
            return buffer[i & mask];
        }

        template<class TFunc>
        inline void Dequeue ( const TFunc& action )
        {
            using namespace natural_threading;
            //assume N is big enough and action is fast enough, so that element will not be overwrite after pop up;
            while ( true )
            {
                T* t = nullptr;
                {
                    Locker lk ( taillocker );
                    if ( tail < head )
                    {
                        t = & ( buffer[tail & mask] );
                        tail ++;
                    }
                    else
                    {
                        break;
                    }
                }
                action ( *t );
            }
        }

        template<class TFunc>
        inline void Dequeue0 ( const TFunc& action )
        {
            using namespace natural_threading;
            //assume N is big enough and action is fast enough, so that element will not be overwrite after pop up;
            while ( true )
            {
                T* t = nullptr;
                {
                    if ( tail < head )
                    {
                        t = & ( buffer[tail & mask] );
                        tail ++;
                    }
                    else
                    {
                        break;
                    }
                }
                action ( *t );
            }
        }

        template<class TFunc>
        inline long Dequeue ( long cursor, const TFunc& action )
        {
            using namespace natural_threading;
            //assume N is big enough and action is fast enough, so that element will not be overwrite after pop up;
            while ( true )
            {
                T* t = nullptr;
                {
                    if ( cursor < head )
                    {
                        t = & ( buffer[cursor & mask] );
                        cursor ++;
                    }
                    else
                    {
                        break;
                    }
                }
                action ( *t );
            }
            return cursor;
        }

        inline int Size() const
        {
            return head - tail;
        }

        inline void Clear()
        {
            head = 0;
            tail = 0;
        }

        inline long Capacity() const
        {
            return N;
        }

    private:
        impl::cacheline_pad_t pad0;
        std::array<T, N> buffer;
        impl::cacheline_pad_t pad1;
        volatile long head = 0;
        impl::cacheline_pad_t pad2;
        volatile long tail = 0;
        impl::cacheline_pad_t pad3;
        long mask = N - 1;
        folly::RWSpinLock headlocker;
        impl::cacheline_pad_t pad4;
        folly::RWSpinLock taillocker;
        impl::cacheline_pad_t pad5;
    };

    template<class T, int N>
    class SPSCRingBuffer
    {
    public:
        SPSCRingBuffer()
        {
        }

        static_assert ( ( ( N > 0 ) && ( ( N & ( ~N + 1 ) ) == N ) ),
                        "SPSCRingBuffer's size must be a positive power of 2" );

        inline void Enqueue ( const T& t )
        {
            using namespace natural_threading;
            buffer[head & mask] = t;
            head++;
        }

        template<class TFunc>
        inline void Emplace ( const TFunc& action )
        {
            using namespace natural_threading;
            action ( buffer[head & mask] );
            head++;
        }

        inline long GetLatestEntryIndex() const
        {
            return head - 1;
        }

        inline const T& GetLatestEntryToRead() const
        {
            return this->operator[] ( GetLatestEntryIndex() );
        }

        inline const T& operator [] ( long i ) const
        {
            return buffer[i & mask];
        }


        template<class TFunc>
        inline void Dequeue ( const TFunc& action )
        {
            using namespace natural_threading;
            //assume N is big enough and action is fast enough, so that element will not be overwrite after pop up;
            while ( true )
            {
                T* t = nullptr;
                {
                    if ( tail < head )
                    {
                        t = & ( buffer[tail & mask] );
                        tail ++;
                    }
                    else
                    {
                        break;
                    }
                }
                action ( *t );
            }
        }

        template<class TFunc>
        inline long Dequeue ( long cursor, const TFunc& action )
        {
            using namespace natural_threading;
            //assume N is big enough and action is fast enough, so that element will not be overwrite after pop up;
            while ( true )
            {
                T* t = nullptr;
                {
                    if ( cursor < head )
                    {
                        t = & ( buffer[cursor & mask] );
                        cursor ++;
                    }
                    else
                    {
                        break;
                    }
                }
                action ( *t );
            }
            return cursor;
        }

        inline int Size() const
        {
            return head - tail;
        }

        inline void Clear()
        {
            head = 0;
            tail = 0;
        }

        inline long Capacity() const
        {
            return N;
        }

    private:
        impl::cacheline_pad_t pad0;
        std::array<T, N> buffer;
        impl::cacheline_pad_t pad1;
        volatile long head = 0;
        impl::cacheline_pad_t pad2;
        volatile long tail = 0;
        impl::cacheline_pad_t pad3;
        long mask = N - 1;
        impl::cacheline_pad_t pad4;
    };

    template<class T, int N>
    class MPSCRingBuffer
    {
    public:
        MPSCRingBuffer() : head ( 0 )
        {
        }

        static_assert ( ( ( N > 0 ) && ( ( N & ( ~N + 1 ) ) == N ) ),
                        "MPSCRingBuffer's size must be a positive power of 2" );

        inline void Enqueue ( const T& t )
        {
            using namespace natural_threading;
            long pos = head++ & mask;;
            buffer[pos] = t;
            buffer_status[pos].seq = true;
        }

        template<class TFunc>
        inline void Emplace ( const TFunc& action )
        {
            using namespace natural_threading;
            long pos = head++ & mask;
            action ( buffer[pos] );
            buffer_status[pos].seq = true;
        }

        inline long GetLatestEntryIndex() const
        {
            return head - 1;
        }

        inline const T& GetLatestEntryToRead() const
        {
            return this->operator[] ( GetLatestEntryIndex() );
        }

        inline const T& operator [] ( long i ) const
        {
            return buffer[i & mask];
        }

        template<class TFunc>
        inline void Dequeue ( const TFunc& action )
        {
            using namespace natural_threading;
            //assume N is big enough and action is fast enough, so that element will not be overwrite after pop up;
            while ( true )
            {
                T* t = nullptr;
                {
                    long pos = tail & mask;
                    if ( buffer_status[pos].seq )
                    {
                        t = & ( buffer[pos] );
                        if ( head - ( tail++ ) < N )
                        {
                            buffer_status[pos].seq = false;
                        }
                    }
                    else
                    {
                        break;
                    }
                }
                action ( *t );
            }
        }

        inline int Size() const
        {
            return head - tail;
        }

        inline void Clear()
        {
            head = 0;
            tail = 0;
        }

        inline long Capacity() const
        {
            return N;
        }

    private:
        struct Status
        {
            volatile bool seq = false;
        };

        impl::cacheline_pad_t pad0;
        std::array<T, N> buffer;
        impl::cacheline_pad_t pad1;
        std::atomic_long head ;
        impl::cacheline_pad_t pad2;
        volatile long tail = 0;
        impl::cacheline_pad_t pad3;
        long mask = N - 1;
        impl::cacheline_pad_t pad4;
        std::array<Status, N> buffer_status;
        impl::cacheline_pad_t pad5;
    };

    template<class T, int N>
    class MPMCRingBuffer
    {
    public:
        //only implement message queue. there is no need for working queue at present
        MPMCRingBuffer() : head ( 0 )
        {
            shift = std::log2 ( N );
            for ( int i = 0 ; i < N; i++ )
            {
                buffer_status[i].seq = i;
            }
        }

        static_assert ( ( ( N > 0 ) && ( ( N & ( ~N + 1 ) ) == N ) ),
                        "MPMCRingBuffer's size must be a positive power of 2" );

        inline void Enqueue ( const T& t )
        {
            using namespace natural_threading;

            long current_head = head++;
            long pos = current_head & mask;
            buffer[pos] = t;
            buffer_status[pos].seq = current_head + N;
        }

        template<class TFunc>
        inline void Emplace ( const TFunc& action )
        {
            using namespace natural_threading;
            long current_head = head++;
            long pos = current_head & mask;
            action ( buffer[pos] );
            buffer_status[pos].seq = current_head + N;
        }

        inline long GetLatestEntryIndex() const
        {
            return head - 1;
        }

        inline const T& GetLatestEntryToRead() const
        {
            return this->operator[] ( GetLatestEntryIndex() );
        }

        inline const T& operator [] ( long i ) const
        {
            return buffer[i & mask];
        }

        template<class TFunc>
        inline void Dequeue0 ( const TFunc& action )
        {
            using namespace natural_threading;
            //assume N is big enough and action is fast enough, so that element will not be overwrite after pop up;
            while ( true )
            {
                T* t = nullptr;
                {
                    long pos = tail & mask;
                    if ( tail < buffer_status[pos].seq )
                    {
                        t = & ( buffer[pos] );
                        tail ++;
                    }
                    else
                    {
                        break;
                    }
                }
                action ( *t );
            }
        }

        template<class TFunc>
        inline long Dequeue ( long cursor, const TFunc& action )
        {
            using namespace natural_threading;
            //assume N is big enough and action is fast enough, so that element will not be overwrite after pop up;
            while ( true )
            {
                T* t = nullptr;
                {
                    long pos = cursor & mask;
                    if ( cursor < buffer_status[pos].seq )
                    {
                        t = & ( buffer[pos] );
                        cursor ++;
                    }
                    else
                    {
                        break;
                    }
                }
                action ( *t );
            }
            return cursor;
        }

        inline int Size() const
        {
            return head - tail;
        }

        inline void Clear()
        {
            head = 0;
            tail = 0;
            for ( int i = 0 ; i < N; i++ )
            {
                buffer_status[i].seq = i;
            }
        }

        inline long Capacity() const
        {
            return N;
        }

    private:
        struct Status
        {
            volatile long seq = 0;
        };

        impl::cacheline_pad_t pad0;
        std::array<T, N> buffer;
        impl::cacheline_pad_t pad1;
        std::atomic_long head ;
        impl::cacheline_pad_t pad2;
        volatile long tail = 0;
        impl::cacheline_pad_t pad3;
        long mask = N - 1;
        long shift = 0;
        impl::cacheline_pad_t pad4;
        std::array<Status, N> buffer_status;
        impl::cacheline_pad_t pad5;
    };

    template<class T, int N>
    class CircularArray
    {
    public:
        CircularArray()
        {
        }

        static_assert ( ( ( N > 0 ) && ( ( N & ( ~N + 1 ) ) == N ) ),
                        "CircularArray's size must be a positive power of 2" );

        inline void Enqueue ( const T& t )
        {
            using namespace natural_threading;
            Locker lk ( headlocker );
            buffer[head & mask] = t;
            head++;
        }

        template<class TFunc>
        inline void Emplace ( const TFunc& action )
        {
            using namespace natural_threading;
            Locker lk ( headlocker );
            action ( buffer[head & mask] );
            head++;
        }

        inline long GetLatestEntryIndex() const
        {
            return head - 1;
        }

        inline const T& GetLatestEntryToRead() const
        {
            return this->operator[] ( GetLatestEntryIndex() );
        }

        inline const T& operator [] ( long i ) const
        {
            return buffer[i & mask];
        }

        template<class TFunc>
        inline void Dequeue ( const TFunc& action )
        {
            using namespace natural_threading;
            //assume N is big enough and action is fast enough, so that element will not be overwrite after pop up;
            while ( true )
            {
                T* t = nullptr;
                {
                    Locker lk ( taillocker );
                    if ( tail < head )
                    {
                        t = & ( buffer[tail & mask] );
                        tail ++;
                    }
                    else
                    {
                        break;
                    }
                }
                action ( *t );
            }
        }

        template<class TFunc>
        inline void Dequeue0 ( const TFunc& action )
        {
            using namespace natural_threading;
            //assume N is big enough and action is fast enough, so that element will not be overwrite after pop up;
            while ( true )
            {
                T* t = nullptr;
                {
                    if ( tail < head )
                    {
                        t = & ( buffer[tail & mask] );
                        tail ++;
                    }
                    else
                    {
                        break;
                    }
                }
                action ( *t );
            }
        }

        template<class TFunc>
        inline long Dequeue ( long cursor, const TFunc& action )
        {
            using namespace natural_threading;
            //assume N is big enough and action is fast enough, so that element will not be overwrite after pop up;
            while ( true )
            {
                T* t = nullptr;
                {
                    if ( cursor < head )
                    {
                        t = & ( buffer[cursor & mask] );
                        cursor ++;
                    }
                    else
                    {
                        break;
                    }
                }
                action ( *t );
            }
            return cursor;
        }

        inline int Size() const
        {
            return head - tail;
        }

        inline void Clear()
        {
            head = 0;
            tail = 0;
        }

        inline long Capacity() const
        {
            return N;
        }

    private:
        impl::cacheline_pad_t pad0;
        std::array<T, N> buffer;
        impl::cacheline_pad_t pad1;
        volatile long head = 0;
        impl::cacheline_pad_t pad2;
        volatile long tail = 0;
        impl::cacheline_pad_t pad3;
        long mask = N - 1;
        folly::RWSpinLock headlocker;
        impl::cacheline_pad_t pad4;
        folly::RWSpinLock taillocker;
        impl::cacheline_pad_t pad5;
    };


}
}
}



#endif //LIBTRADE_CONTAINER_H
