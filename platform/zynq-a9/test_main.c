/* SPDX-License-Identifier: MIT */
/*
 * Unit test entry for Zynq-A9 QEMU.
 * Runs all test suites inside a FreeRTOS task with tick interrupts
 * enabled, so OSAL primitives (mutex, queue) work correctly.
 *
 * Result signaled via UART marker: ZYNQ_TEST_EXIT:PASS/FAIL.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "xscugic.h"
#include "xscutimer.h"
#include "xparameters.h"

#include "tests/unit/framework/test.h"

/* All test suites */
extern int test_ai_memory_suite(void);
extern int test_gateway_suite(void);
extern int test_tools_suite(void);
extern int test_im_util_suite(void);
extern int test_ota_suite(void);

/* GIC instance (needed by tick config) */
XScuGic xInterruptController;
static XScuTimer xTimer;

#define TIMER_CLOCK_HZ \
    (XPAR_CPU_CORTEXA9_0_CPU_CLK_FREQ_HZ / 2)

static void test_task(void *pvParameters)
{
    (void)pvParameters;
    int failed = 0;

    printf("\n========== rt-claw unit tests ==========\n\n");

    failed += test_ai_memory_suite();
    failed += test_im_util_suite();
    failed += test_gateway_suite();
    failed += test_tools_suite();
    failed += test_ota_suite();

    printf("\n========================================\n");
    if (failed) {
        printf("FAILED: %d suite(s) had failures\n", failed);
        printf("ZYNQ_TEST_EXIT:FAIL\n");
    } else {
        printf("ALL SUITES PASSED\n");
        printf("ZYNQ_TEST_EXIT:PASS\n");
    }

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

int main(void)
{
    printf("rt-claw unit tests (Zynq-A9 QEMU, FreeRTOS)\n");

    xTaskCreate(test_task, "test", 8192, NULL, 2, NULL);
    vTaskStartScheduler();

    for (;;) {
    }

    return 0;
}

/* ---- FreeRTOS hooks ---- */

void vApplicationMallocFailedHook(void)
{
    printf("FATAL: malloc failed\n");
    printf("ZYNQ_TEST_EXIT:FAIL\n");
    for (;;) {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    printf("FATAL: stack overflow in '%s'\n", pcTaskName);
    printf("ZYNQ_TEST_EXIT:FAIL\n");
    for (;;) {
    }
}

void vApplicationIdleHook(void) {}

/* FreeRTOS+TCP stubs for test build */
void vApplicationIPNetworkEventHook(int e) { (void)e; }
void vApplicationIPNetworkEventHook_Multi(int e, void *ep)
{
    (void)e; (void)ep;
}
int xApplicationDNSQueryHook_Multi(void *ep, const char *n)
{
    (void)ep; (void)n; return 0;
}

void vAssertCalled(const char *pcFile, unsigned long ulLine)
{
    printf("ASSERT: %s:%lu\n", pcFile, ulLine);
    printf("ZYNQ_TEST_EXIT:FAIL\n");
    for (;;) {
    }
}

/* ---- Tick timer (SCU private timer via GIC) ---- */

void vConfigureTickInterrupt(void)
{
    XScuGic_Config *pxGICConfig;
    XScuTimer_Config *pxTimerConfig;
    BaseType_t xStatus;

    pxGICConfig = XScuGic_LookupConfig(XPAR_SCUGIC_SINGLE_DEVICE_ID);
    configASSERT(pxGICConfig != NULL);

    xStatus = XScuGic_CfgInitialize(&xInterruptController,
                                     pxGICConfig,
                                     pxGICConfig->CpuBaseAddress);
    configASSERT(xStatus == XST_SUCCESS);
    (void)xStatus;

    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_IRQ_INT,
        (Xil_ExceptionHandler)XScuGic_InterruptHandler,
        &xInterruptController);

    extern void FreeRTOS_Tick_Handler(void);
    xStatus = XScuGic_Connect(&xInterruptController,
                               XPAR_SCUTIMER_INTR,
                               (Xil_ExceptionHandler)FreeRTOS_Tick_Handler,
                               (void *)&xTimer);
    configASSERT(xStatus == XST_SUCCESS);

    pxTimerConfig = XScuTimer_LookupConfig(XPAR_SCUTIMER_DEVICE_ID);
    configASSERT(pxTimerConfig != NULL);

    xStatus = XScuTimer_CfgInitialize(&xTimer, pxTimerConfig,
                                       pxTimerConfig->BaseAddr);
    configASSERT(xStatus == XST_SUCCESS);

    XScuTimer_EnableAutoReload(&xTimer);
    XScuTimer_SetPrescaler(&xTimer, 0);
    XScuTimer_LoadTimer(&xTimer,
                        (TIMER_CLOCK_HZ / configTICK_RATE_HZ) - 1);
    XScuGic_SetPriorityTriggerType(&xInterruptController,
                                    XPAR_SCUTIMER_INTR, 0xA0, 0x03);
    XScuGic_Enable(&xInterruptController, XPAR_SCUTIMER_INTR);
    XScuTimer_EnableInterrupt(&xTimer);
    XScuTimer_Start(&xTimer);
    Xil_ExceptionEnable();
}

void vClearTickInterrupt(void)
{
    XScuTimer_ClearInterruptStatus(&xTimer);
}

void vApplicationFPUSafeIRQHandler(uint32_t ulICCIAR)
{
    uint32_t ulInterruptID = ulICCIAR & 0x3FFUL;
    const XScuGic_Config *pxConfig = xInterruptController.Config;

    if (ulInterruptID < XSCUGIC_MAX_NUM_INTR_INPUTS) {
        const XScuGic_VectorTableEntry *pxEntry =
            &pxConfig->HandlerTable[ulInterruptID];
        pxEntry->Handler(pxEntry->CallBackRef);
    }
}
