/*------------------------------------------------------------------------------
 * MDK Middleware - Component ::USB:Host
 * Copyright (c) 2004-2024 Arm Limited (or its affiliates). All rights reserved.
 *------------------------------------------------------------------------------
 * Name:    usbh_cmsis_rtos2.c
 * Purpose: USB Host (USBH) - RTOS abstraction implemented on CMSIS-RTOS2
 *----------------------------------------------------------------------------*/

#include "usbh_cmsis_rtos2.h"

#include <string.h>

// Resources definition

// Import thread function prototypes
extern void USBH_Core_Thread      (void *arg);
extern void USBH_CDC_IntIn_Thread (void *arg);
extern void USBH_HID_IntIn_Thread (void *arg);

// Import thread definitions generated by each Host Controller instance
// in usbh_config_hc_n.c file (n = 0 .. 3)
#ifdef RTE_USB_Host_0
extern const osThreadAttr_t usbh0_core_thread_attr;
#endif
#ifdef RTE_USB_Host_1
extern const osThreadAttr_t usbh1_core_thread_attr;
#endif
#ifdef RTE_USB_Host_2
extern const osThreadAttr_t usbh2_core_thread_attr;
#endif
#ifdef RTE_USB_Host_3
extern const osThreadAttr_t usbh3_core_thread_attr;
#endif

static const osThreadAttr_t * const usbh_core_thread_attr[USBH_HC_NUM] = {
#ifdef RTE_USB_Host_0
    &usbh0_core_thread_attr 
#endif
#ifdef RTE_USB_Host_1
  , &usbh1_core_thread_attr
#endif
#ifdef RTE_USB_Host_2
  , &usbh2_core_thread_attr
#endif
#ifdef RTE_USB_Host_3
  , &usbh3_core_thread_attr
#endif
};


// Create thread definitions for classes requiring threads
#if (USBH_CDC_NUM > 0)
static const char *usbh_cdc_thread_name        [USBH_CDC_NUM] = {
#if (USBH_CDC_NUM > 0)
    "USBH_CDC0_IntIn_Thread"
#endif
#if (USBH_CDC_NUM > 1)
  , "USBH_CDC1_IntIn_Thread"
#endif
#if (USBH_CDC_NUM > 2)
  , "USBH_CDC2_IntIn_Thread"
#endif
#if (USBH_CDC_NUM > 3)
  , "USBH_CDC3_IntIn_Thread"
#endif
};
#ifdef RTE_CMSIS_RTOS2_RTX5
static osRtxThread_t  usbh_cdc_thread_cb_mem   [USBH_CDC_NUM]                                                __attribute__((section(".bss.os.thread.cb")));
static uint64_t       usbh_cdc_thread_stack_mem[USBH_CDC_NUM][(USBH_CDC_INT_IN_THREAD_STACK_SIZE + 7U) / 8U] __attribute__((section(".bss.os.thread.stack")));
#endif
#endif

#if (USBH_HID_NUM > 0)
static const char *usbh_hid_thread_name        [USBH_HID_NUM] = {
#if (USBH_HID_NUM > 0)
    "USBH_HID0_IntIn_Thread"
#endif
#if (USBH_HID_NUM > 1)
  , "USBH_HID1_IntIn_Thread"
#endif
#if (USBH_HID_NUM > 2)
  , "USBH_HID2_IntIn_Thread"
#endif
#if (USBH_HID_NUM > 3)
  , "USBH_HID3_IntIn_Thread"
#endif
};
#ifdef RTE_CMSIS_RTOS2_RTX5
static osRtxThread_t  usbh_hid_thread_cb_mem   [USBH_HID_NUM]                                                __attribute__((section(".bss.os.thread.cb")));
static uint64_t       usbh_hid_thread_stack_mem[USBH_HID_NUM][(USBH_HID_INT_IN_THREAD_STACK_SIZE + 7U) / 8U] __attribute__((section(".bss.os.thread.stack")));
#endif
#endif


// Create timer definitions
#ifdef RTE_CMSIS_RTOS2_RTX5
static osRtxTimer_t   usbh_debounce_timer_cb_mem[USBH_HC_NUM] __attribute__((section(".bss.os.timer.cb")));
#endif


// Create mutex definitions
#ifdef RTE_CMSIS_RTOS2_RTX5
static osRtxMutex_t   usbh_def_pipe_mutex_cb_mem[USBH_HC_NUM] __attribute__((section(".bss.os.mutex.cb")));
#endif
static osMutexAttr_t  usbh_def_pipe_mutex_attr = {
  NULL,
  osMutexRecursive,
#ifdef RTE_CMSIS_RTOS2_RTX5
  &usbh_def_pipe_mutex_cb_mem,
  sizeof(osRtxMutex_t)
#else
  NULL,
  0U
#endif
};


// Create semaphore definitions
#ifdef RTE_CMSIS_RTOS2_RTX5
static osRtxSemaphore_t usbh_driver_semaphore_cb_mem[USBH_HC_NUM] __attribute__((section(".bss.os.semaphore.cb")));
#endif
static osSemaphoreAttr_t usbh_driver_semaphore_attr = {
  NULL,
  0U,
#ifdef RTE_CMSIS_RTOS2_RTX5
  &usbh_driver_semaphore_cb_mem,
  sizeof(osRtxSemaphore_t)
#else
  NULL,
  0U
#endif
};


// Functions definition

/// \brief Create a thread
/// \param[in]     thread               thread
/// \param[in]     index                parameter dependent on thread (controller index or class instance)
/// \return
///                value != 0:          handle to created thread
///                value = 0:           thread creation failed
void *USBH_ThreadCreate (usbhThread_t thread, uint8_t index) {
#if (((USBH_CDC_NUM          > 0) || \
      (USBH_HID_NUM          > 0)) )
  osThreadAttr_t thread_attr;

  memset(&thread_attr, 0U, sizeof(osThreadAttr_t));
#endif

  switch (thread) {
    case usbhThreadCore:
      if (index >= USBH_HC_NUM) { return NULL; }
      return ((void *)osThreadNew (USBH_Core_Thread, (void *)((uint32_t)index), usbh_core_thread_attr[index]));

    case usbhThreadCDC:
#if (USBH_CDC_NUM > 0)
      if (index >= USBH_CDC_NUM) { return NULL; }
      thread_attr.name       =  usbh_cdc_thread_name[index];
#ifdef RTE_CMSIS_RTOS2_RTX5
      thread_attr.cb_mem     = &usbh_cdc_thread_cb_mem[index];
      thread_attr.cb_size    =  sizeof(osRtxThread_t);
      thread_attr.stack_mem  = &usbh_cdc_thread_stack_mem[index][0];
#endif
      thread_attr.stack_size =  ((USBH_CDC_INT_IN_THREAD_STACK_SIZE + 7U) / 8U) * 8U;
      thread_attr.priority   =    USBH_CDC_INT_IN_THREAD_PRIORITY;
      return ((void *)osThreadNew (USBH_CDC_IntIn_Thread, (void *)((uint32_t)index), &thread_attr));
#else
      return NULL;
#endif

    case usbhThreadHID:
#if (USBH_HID_NUM > 0)
      if (index >= USBH_HID_NUM) { return NULL; }
      thread_attr.name       =  usbh_hid_thread_name[index];
#ifdef RTE_CMSIS_RTOS2_RTX5
      thread_attr.cb_mem     = &usbh_hid_thread_cb_mem[index];
      thread_attr.cb_size    =  sizeof(osRtxThread_t);
      thread_attr.stack_mem  = &usbh_hid_thread_stack_mem[index][0];
#endif
      thread_attr.stack_size =  ((USBH_HID_INT_IN_THREAD_STACK_SIZE + 7U) / 8U) * 8U;
      thread_attr.priority   =    USBH_HID_INT_IN_THREAD_PRIORITY;
      return ((void *)osThreadNew (USBH_HID_IntIn_Thread, (void *)((uint32_t)index), &thread_attr));
#else
      return NULL;
#endif
  }
  return NULL;
}

/// \brief Get handle to currently running thread
/// \return
///                value != 0:          handle to currently running thread
///                value = 0:           error
void *USBH_ThreadGetHandle (void) {
  return ((void *)osThreadGetId ());
}

/// \brief Terminate execution of a thread
/// \param[in]     thread_hndl          thread handle
/// \return
///                value 0:             thread terminated successfully
///                value < 0:           thread termination failed
int32_t USBH_ThreadTerminate (void *thread_hndl) {
  if (thread_hndl == NULL) { return -1; }
  return (osThreadTerminate ((osThreadId_t)thread_hndl));
}

/// \brief Helper function to convert ms to ticks
/// \param[in]     ms                   number of milliseconds
/// \return        ticks
static uint32_t USBH_MsToTick (uint32_t ms) {
  static uint32_t tick_freq_mul = 0U;
  static uint32_t max_ms;

  if (tick_freq_mul == 0U) {
    // Initialize helper variables once
    tick_freq_mul = (osKernelGetTickFreq () * 1024U) / 1000U;
    max_ms        = (0xFFFFFFFFU - 1023U) / tick_freq_mul;
  }

  if ((ms == 0U) || (ms == osWaitForever) || (tick_freq_mul == 1024U)) {
    return ms;
  }

  if (ms > max_ms) {
    return (osWaitForever - 1U);
  }

  return (((ms * tick_freq_mul) + 1023U) / 1024U);
}

/// \brief Delay execution of a thread for specified number of milliseconds
/// \param[in]     millisec             number of milliseconds
/// \return
///                value 0:             delay finished
///                value < 0:           delay failed
int32_t USBH_Delay (uint32_t millisec) {
  return  (osDelay (USBH_MsToTick(millisec)));
}

/// \brief Create and initialize a single-shot timer for connection debouncing
/// \param[in]     ctrl                 controller index
/// \return
///                value != 0:          handle to created timer
///                value = 0:           timer creation failed
void *USBH_TimerCreate (uint8_t ctrl) {
  osTimerAttr_t timer_attr;

  if (ctrl >= USBH_HC_NUM) { return NULL; }
  memset(&timer_attr, 0U, sizeof(osTimerAttr_t));
#ifdef RTE_CMSIS_RTOS2_RTX5
  timer_attr.cb_mem  = &usbh_debounce_timer_cb_mem[ctrl];
  timer_attr.cb_size =  sizeof(osRtxTimer_t);
#endif
  return ((void *)osTimerNew ((osTimerFunc_t)USBH_ConnectDebounce, osTimerOnce, (void *)((uint32_t)ctrl), &timer_attr));
}

/// \brief Start or restart a timer
/// \param[in]     timer_hndl           timer handle
/// \return
///                value 0:             timer started or restarted successfully
///                value < 0:           timer start or restart failed
int32_t USBH_TimerStart (void *timer_hndl, uint32_t millisec) {
  if (timer_hndl == NULL) { return -1; }
  return  (osTimerStart ((osTimerId_t)timer_hndl, USBH_MsToTick(millisec)));
}

/// \brief Stop a timer
/// \param[in]     timer_hndl           timer handle
/// \return
///                value 0:             timer stopped successfully
///                value < 0:           timer stop failed
int32_t USBH_TimerStop (void *timer_hndl) {
  if (timer_hndl == NULL) { return -1; }
  if (osTimerIsRunning((osTimerId_t)timer_hndl) == 0) {
    return 0;
  }
  return (osTimerStop ((osTimerId_t)timer_hndl));
}

/// \brief Delete a timer
/// \param[in]     timer_hndl           timer handle
/// \return
///                value 0:             timer deleted successfully
///                value < 0:           timer deletion failed
int32_t USBH_TimerDelete (void *timer_hndl) {
  if (timer_hndl == NULL) { return -1; }
  return (osTimerDelete ((osTimerId_t)timer_hndl));
}

/// \brief Set the specified flags of a thread
/// \param[in]     thread_hndl          thread handle
/// \param[in]     flags                flags to be set
/// \return
///                value 0:             flags set successfully
///                value >= 0x80000000: setting of flags failed
uint32_t USBH_ThreadFlagsSet (void *thread_hndl, uint32_t flags) {
  if (thread_hndl == NULL) { return 0U; }
  return ((uint32_t)osThreadFlagsSet ((osThreadId_t)thread_hndl, flags));
}

/// \brief Wait for any USB related flag of currently running thread to become signaled
/// \param[in]     millisec             time-out in milliseconds, or 0 in case of no time-out
/// \return
///                value < 0x80000000:  flags
///                value 0:             time-out
///                value >= 0x80000000: error
uint32_t USBH_ThreadFlagsWait (uint32_t millisec) {
  uint32_t res;

  res = osThreadFlagsWait (0x1FFFU, osFlagsWaitAny, USBH_MsToTick(millisec));
  if ((res & 0x80000000U) == 0U) {
    if ((res & ~0x1FFFU) != 0U) {
      osThreadFlagsSet (osThreadGetId (), res & (~0x1FFFU));    // Resend flags if not USB flags
    }
    return (res & 0x1FFFU);
  } else if (res == (uint32_t)osErrorTimeout) {
    return 0U;
  } else {
    return 0x80000000U;
  }
}

/// \brief Create and initialize a mutex
/// \param[in]     mutex                mutex
/// \param[in]     ctrl                 controller index
/// \return
///                value != 0:          handle to created mutex
///                value = 0:           mutex creation failed
void *USBH_MutexCreate (usbhMutex_t mutex, uint8_t ctrl) {
  if (mutex == usbhMutexCore) {
    if (ctrl >= USBH_HC_NUM) { return NULL; }
#ifdef RTE_CMSIS_RTOS2_RTX5
    usbh_def_pipe_mutex_attr.cb_mem = &usbh_def_pipe_mutex_cb_mem[ctrl];
#endif
    return ((void *)osMutexNew (&usbh_def_pipe_mutex_attr));
  }
  return NULL;
}

/// \brief Acquire a mutex or timeout if it is locked
/// \param[in]     mutex_hndl           mutex handle
/// \param[in]     millisec             time-out in milliseconds, or 0 in case of no time-out
/// \return
///                value 0:             mutex acquired successfully
///                value < 0:           mutex acquire failed
int32_t USBH_MutexAcquire (void *mutex_hndl, uint32_t millisec) {
  if (mutex_hndl == NULL) { return -1; }
  return  (osMutexAcquire ((osMutexId_t)mutex_hndl, USBH_MsToTick(millisec)));
}

/// Release a mutex
/// \param[in]     mutex_hndl           mutex handle
/// \return
///                value 0:             mutex released successfully
///                value < 0:           mutex release failed
int32_t USBH_MutexRelease (void *mutex_hndl) {
  if (mutex_hndl == NULL) { return -1; }
  return (osMutexRelease ((osMutexId_t)mutex_hndl));
}

/// \brief Delete a mutex
/// \param[in]     mutex_hndl           mutex handle
/// \return
///                value 0:             mutex deleted successfully
///                value < 0:           mutex deletion failed
int32_t USBH_MutexDelete (void *mutex_hndl) {
  if (mutex_hndl == NULL) { return -1; }
  return (osMutexDelete ((osMutexId_t)mutex_hndl));
}

/// \brief Create and initialize a binary semaphore
/// \param[in]     semaphore            semaphore
/// \param[in]     ctrl                 controller index
/// \return
///                value != 0:          handle to created semaphore
///                value = 0:           semaphore creation failed
void *USBH_SemaphoreCreate (usbhSemaphore_t semaphore, uint8_t ctrl) {
  if (semaphore == usbhSemaphoreCore) {
    if (ctrl >= USBH_HC_NUM) { return NULL; }
#ifdef RTE_CMSIS_RTOS2_RTX5
    usbh_driver_semaphore_attr.cb_mem = &usbh_driver_semaphore_cb_mem[ctrl];
#endif
    return ((void *)osSemaphoreNew (1U, 1U, &usbh_driver_semaphore_attr));
  }
  return NULL;
}

/// \brief Wait for a semaphore token to become available and acquire it
/// \param[in]     semaphore_hndl       semaphore handle
/// \param[in]     millisec             time-out in milliseconds, or 0 in case of no time-out
/// \return
///                value 0:             token acquired successfully
///                value < 0:           token acquire failed
int32_t USBH_SemaphoreAcquire (void *semaphore_hndl, uint32_t millisec) {
  if (semaphore_hndl == NULL) { return -1; }
  return  (osSemaphoreAcquire ((osSemaphoreId_t)semaphore_hndl, USBH_MsToTick(millisec)));
}

/// \brief Release a semaphore token
/// \param[in]     semaphore_hndl       semaphore handle
/// \return
///                value 0:             token released successfully
///                value < 0:           token release failed
int32_t USBH_SemaphoreRelease (void *semaphore_hndl) {
  if (semaphore_hndl == NULL) { return -1; }
  return (osSemaphoreRelease ((osSemaphoreId_t)semaphore_hndl));
}

/// \brief Delete a semaphore
/// \param[in]     semaphore_hndl       semaphore handle
/// \return
///                value 0:             semaphore deleted successfully
///                value < 0:           semaphore deletion failed
int32_t USBH_SemaphoreDelete (void *semaphore_hndl) {
  if (semaphore_hndl == NULL) { return -1; }
  return (osSemaphoreDelete ((osSemaphoreId_t)semaphore_hndl));
}
