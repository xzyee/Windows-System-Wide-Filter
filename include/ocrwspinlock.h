/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved
Revision history:
04.12.2006 ( December )
 Start
*/

#if !defined(_OC_RWLOCK_H_)
#define _OC_RWLOCK_H_

#include <ntddk.h>
 /*
 0x01000000 no owner
拿读锁，原子减1， 0x00ffffff，得到锁
拿读锁，原子减1， 0x00fffffe，得到锁
拿读锁，原子减1， 0x00fffffd，得到锁
拿写锁，原子比较不等，spinning！
拿读锁，原子减1， 0x00fffffc，得到锁
另一个线程继续spining...
还读锁，原子加1，0x00fffffd，释放锁成功
还读锁，原子加1，0x00fffffe，释放锁成功
还读锁，原子加1，0x00ffffff，释放锁成功
还有一个读锁，拿写锁的线程继续spinning...
还读锁，原子加1，0x01000000，释放锁成功
现在没有reader了，spinning线程可以获得成功了
拿写锁，原子比较相等，0x0000000，raw成功返回1，spin退出，获得写锁成功！
拿写锁，原子比较不等，0x0000000，raw成功返回0，spinning！不可以两个writer同时存在
拿读锁，不成功，原子数减1后为负数，不成功，所以加1， 0x00000000，不能得到锁，开始打瞌睡
拿读锁，不成功，原子数减1后为负数，不成功，所以加1， 0x00000000，不能得到锁，开始打瞌睡
以后所有拿读锁的都不成功，都打瞌睡
所有拿写锁的也都不成功，都会spinning
此时只有一个writer！
现在这个writer开始还锁了
还写锁，原子加0x01000000，为0x01000000，no user！
现在所有的读锁都在打瞌睡，所有的写锁都在spinning，因此某个写锁得到的概率比较大

所以：
允许有多个读，不允许有多个写
多个读都还回后，写才能进入，读写不会同时存在
只要有一个写的，其他读写都不得满足
只要有一个读的，其他读马上满足，只要不要来写的
只要有一个读的，写的也不能满足，必须等所有的读都没有了
读不到会打瞌睡，有线程切换的开销
写不到会spin，很浪费CPU，短时性能非常好，长时间后果很严重，因为基本上这个线程在DISPATHCER级别上阻塞了
*/
//-----------------------------------------------------------

typedef struct _OC_RW_SPIN_LOCK{
    //
    // RwLock is equal to 0x01000000 for no owner, 
    // 0x00ffffff for one reader, 0x00fffffe for two readers,
    // and 0x00000000 for one writer
    //
    LONG    RwLock; //有符号
} OC_RW_SPIN_LOCK, *POC_RW_SPIN_LOCK;

//-----------------------------------------------------------
//raw行为失败不会改数据
__forceinline
VOID
OcRwInitializeRwLock(
    IN POC_RW_SPIN_LOCK Lock
    )
{
    Lock->RwLock = 0x01000000;//no owner
}

//-----------------------------------------------------------

__forceinline
ULONG
OcRwRawTryLockForRead(
    IN POC_RW_SPIN_LOCK Lock
    )
    /*
    returns 0x1 if lock has been acquired for read
    returns 0x0 if lock has not been acquired for read
    */
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    //
    // step 1 - try to acquire for read
    //
    if( InterlockedDecrement( &Lock->RwLock ) >= 0x0 )
        return 1;
    //
    // the lock has been acquired for write
    // undo the step 1
    //
    InterlockedIncrement( &Lock->RwLock );
    return 0;
}

//-----------------------------------------------------------
//raw行为失败不会改数据
__forceinline
ULONG
OcRwRawTryLockForWrite(
    IN POC_RW_SPIN_LOCK Lock
    )
    /*
    returns 0x1 if lock has been acquired for write
    returns 0x0 if lock has not been acquired for write
    */
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    //
    // step 1 - try to acquire for write
    //
    if( 0x01000000 == InterlockedCompareExchange( &Lock->RwLock, 
                                                  0x00000000, //one writer
                                                  0x01000000 ) )
        return 1;
    //
    // the lock has been acquired for read
    //
    return 0;
}

//-----------------------------------------------------------
//可能会休眠，就是醒来看一眼后继续睡觉
//拿不到不会修改数据
__forceinline
VOID
OcRwAcquireLockForRead( //等待退出0x0,0x0代表一个writer
    IN POC_RW_SPIN_LOCK Lock,
    OUT KIRQL* PtrOldIrql
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    //
    // disable the kernel preemptition
    //
    KeRaiseIrql( DISPATCH_LEVEL, PtrOldIrql );

    //
    // spin until lock is acquired
    //
    while( 0x0 == OcRwRawTryLockForRead( Lock ) ){ //0x0代表数据未曾实质改变
        //
        // to avoid starvaition when 
        // two threads compete for
        // lock I enable the kernel
        // preemption and reschedule 
        // if possible
        //
        KeLowerIrql( *PtrOldIrql );
        if( KeGetCurrentIrql() <= APC_LEVEL ){

            LARGE_INTEGER    Timeout;

            //
            // timeout to 10e-6 sec, i.e. (10e-7)*(10)
            Timeout.QuadPart = -(10i64);

            KeDelayExecutionThread( KernelMode,
                                    FALSE,//内核必须设为false，就是不通知
                                    &Timeout );
        }
        KeRaiseIrql( DISPATCH_LEVEL, PtrOldIrql );
    }//while
}

//-----------------------------------------------------------
//非常快，毕竟是release
__forceinline
VOID
OcRwReleaseReadLock(
    IN POC_RW_SPIN_LOCK Lock,
    IN KIRQL OldIrql
    )
{
    ASSERT( KeGetCurrentIrql() == DISPATCH_LEVEL );

    InterlockedIncrement( &Lock->RwLock );
    KeLowerIrql( OldIrql );
}

//-----------------------------------------------------------
//可能会spin，这样比较费
//spin期间不会改数据
__forceinline
VOID
OcRwAcquireLockForWrite(
    IN POC_RW_SPIN_LOCK Lock,
    OUT KIRQL* PtrOldIrql
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    //
    // disable the kernel preemptition
    //
    KeRaiseIrql( DISPATCH_LEVEL, PtrOldIrql );
    //
    // spin until lock is acquired
    //
    while( 0x0 == OcRwRawTryLockForWrite( Lock ) );
}

//-----------------------------------------------------------
//非常快，毕竟是release
//不可能连续有两个，因为写必须互斥，所以一次性加上0x01000000
__forceinline
VOID
OcRwReleaseWriteLock(
    IN POC_RW_SPIN_LOCK Lock,
    IN KIRQL OldIrql
    )
{
    ASSERT( KeGetCurrentIrql() == DISPATCH_LEVEL );

    /*
    //
    // Try to change in the loop, if I will be out of luck
    // the threads might starve forever. To reduce
    // possibility of such scenario I reschedule waiting 
    // thread in OcRwAcquireLockForRead, if possible.
    //
    while( 0x00000000 != InterlockedCompareExchange( &Lock->RwLock, 
                                                     0x01000000, 
                                                     0x00000000 ) );
                                                     */

    //
    // Simply add 0x01000000 to the current value,
    // others who content for the lock will notice that
    // the lock is free after they restore values after
    // unsuccessful attempts to acquire the lock
    //
    InterlockedExchangeAdd( &Lock->RwLock, 0x01000000 );

    KeLowerIrql( OldIrql );
}

//-----------------------------------------------------------

#endif//_OC_RWLOCK_H_
