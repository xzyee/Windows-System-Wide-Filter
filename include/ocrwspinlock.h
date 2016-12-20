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
