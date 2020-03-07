/* Copyright 2020 Adam Green (https://github.com/adamgreen/)

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
// Library to enable GDB debugging of Thread Mode code running on the Adruino Portenta H7 over
// the USB serial connection.
#include <Arduino.h>
#include "ThreadMRI.h"
extern "C"
{
    #include "core/mri.h"
    #include "core/platforms.h"
    #include "core/semihost.h"
}
// UNDONE: Might not need this.
#include <signal.h>
#include <string.h>

// UNDONE: Switch back to the USB/CDC based serial port.
#undef Serial
#define Serial Serial1

static const char g_memoryMapXml[] = "<?xml version=\"1.0\"?>"
                                     "<!DOCTYPE memory-map PUBLIC \"+//IDN gnu.org//DTD GDB Memory Map V1.0//EN\" \"http://sourceware.org/gdb/gdb-memory-map.dtd\">"
                                     "<memory-map>"
                                     "<memory type=\"ram\" start=\"0x00000000\" length=\"0x10000\"> </memory>"
                                     "<memory type=\"flash\" start=\"0x08000000\" length=\"0x200000\"> <property name=\"blocksize\">0x20000</property></memory>"
                                     "<memory type=\"ram\" start=\"0x10000000\" length=\"0x48000\"> </memory>"
                                     "<memory type=\"ram\" start=\"0x1ff00000\" length=\"0x20000\"> </memory>"
                                     "<memory type=\"ram\" start=\"0x20000000\" length=\"0x20000\"> </memory>"
                                     "<memory type=\"ram\" start=\"0x24000000\" length=\"0x80000\"> </memory>"
                                     "<memory type=\"ram\" start=\"0x30000000\" length=\"0x48000\"> </memory>"
                                     "<memory type=\"ram\" start=\"0x38000000\" length=\"0x10000\"> </memory>"
                                     "<memory type=\"ram\" start=\"0x38800000\" length=\"0x1000\"> </memory>"
                                     "<memory type=\"ram\" start=\"0x58020000\" length=\"0x2c00\"> </memory>"
                                     "<memory type=\"ram\" start=\"0x58024400\" length=\"0xc00\"> </memory>"
                                     "<memory type=\"ram\" start=\"0x58025400\" length=\"0x800\"> </memory>"
                                     "<memory type=\"ram\" start=\"0x58026000\" length=\"0x800\"> </memory>"
                                     "<memory type=\"ram\" start=\"0x58027000\" length=\"0x400\"> </memory>"
                                     "<memory type=\"flash\" start=\"0x90000000\" length=\"0x10000000\"> <property name=\"blocksize\">0x200</property></memory>"
                                     "<memory type=\"ram\" start=\"0xc0000000\" length=\"0x800000\"> </memory>"
                                     "</memory-map>";




ThreadMRI::ThreadMRI()
{
    mriInit("");
}

// UNDONE: This is just for initial bringup.
extern "C" void mriDebugException(void);

void ThreadMRI::debugException()
{
    mriDebugException();
}




// ---------------------------------------------------------------------------------------------------------------------
// Global Platform_* functions needed by MRI to initialize and communicate with MRI.
// These functions will perform most of their work through the DebugSerial singleton.
// ---------------------------------------------------------------------------------------------------------------------
#ifdef UNDONE
// The debugger uses these handlers to catch faults, debug events, etc.
void mriExceptionHandler(void);
void mriFaultHandler(void);

struct SystemHandlerPriorities {
    uint32_t svcallPriority;
    uint32_t pendsvPriority;
    uint32_t systickPriority;
};

// Forward Function Declarations
static SystemHandlerPriorities getSystemHandlerPrioritiesBeforeMriModifiesThem();
static void lowerPriorityOfNonDebugHandlers(const SystemHandlerPriorities* pPriorities);
static void setHandlerPriorityLowerThanDebugger(IRQn_Type irq, uint32_t origPriority);
static void switchFaultHandlersToDebugger();
#endif // UNDONE


void Platform_Init(Token* pParameterTokens)
{
#ifdef UNDONE
    SystemHandlerPriorities origPriorities = getSystemHandlerPrioritiesBeforeMriModifiesThem();

    __try
        mriCortexMInit((Token*)pParameterTokens);
    __catch
        __rethrow;

    // UNDONE: Might want to always keep the USB handler at elevated priority.
    // Set interrupt used by serial comms (UART or USB) at highest priority.
    // Set DebugMonitor interrupt at next highest priority.
    // Set all other external interrupts lower than both serial comms and DebugMonitor.
    lowerPriorityOfNonDebugHandlers(&origPriorities);
    g_pDebugSerial->setSerialPriority(0);
    NVIC_SetPriority(DebugMonitor_IRQn, 1);

    switchFaultHandlersToDebugger();
#endif // UNDONE
}

#ifdef UNDONE
static SystemHandlerPriorities getSystemHandlerPrioritiesBeforeMriModifiesThem() {
    SystemHandlerPriorities priorities;

    priorities.svcallPriority = NVIC_GetPriority(SVCall_IRQn);
    priorities.pendsvPriority = NVIC_GetPriority(PendSV_IRQn);
    priorities.systickPriority = NVIC_GetPriority(SysTick_IRQn);
    return priorities;
}

static void lowerPriorityOfNonDebugHandlers(const SystemHandlerPriorities* pPriorities) {
    // Set priority of system handlers that aren't directly related to debugging lower than those that are.
    setHandlerPriorityLowerThanDebugger(SVCall_IRQn, pPriorities->svcallPriority);
    setHandlerPriorityLowerThanDebugger(PendSV_IRQn, pPriorities->pendsvPriority);
    setHandlerPriorityLowerThanDebugger(SysTick_IRQn, pPriorities->systickPriority);

    // Do the same for external interrupts.
    for (int irq = WWDG_IRQn ; irq <= WAKEUP_PIN_IRQn ; irq++) {
        setHandlerPriorityLowerThanDebugger((IRQn_Type)irq, NVIC_GetPriority((IRQn_Type)irq));
    }
}

static void setHandlerPriorityLowerThanDebugger(IRQn_Type irq, uint32_t priority)
{
    // There are a total of 16 priority levels on the STM32H747XI,
    // 4 of them reserved:
    // 0 - Highest - Communication Peripheral ISR
    // 1           - DebugMon
    // 14          - SVCall
    // 15 - Lowest - PendSV & SysTick for switching context
    //
    // Everything not listed above will be lowered in priority by 2 to make room for the debugger priorities
    // except that ISRs that are already at priorities 12 & 13 will not be altered or they would conflict
    // with the 2 lowest reserved priorities.
    uint32_t highestPriority = (1 << __NVIC_PRIO_BITS) - 1;
    if (priority <= highestPriority-4) {
        priority += 2;
    }
    NVIC_SetPriority(irq, priority);
}

static void switchFaultHandlersToDebugger(void) {
    NVIC_SetVector(HardFault_IRQn,        (uint32_t)&mriFaultHandler);
    NVIC_SetVector(MemoryManagement_IRQn, (uint32_t)&mriFaultHandler);
    NVIC_SetVector(BusFault_IRQn,         (uint32_t)&mriFaultHandler);
    NVIC_SetVector(UsageFault_IRQn,       (uint32_t)&mriExceptionHandler);
}
#endif // UNDONE

uint32_t Platform_CommHasReceiveData(void)
{
    return Serial.available();
}

int Platform_CommReceiveChar(void) {
    while (!Serial.available())
    {
        // Busy wait.
    }
    return Serial.read();
}

void Platform_CommSendChar(int character) {
    Serial.write(character);
}

int Platform_CommCausedInterrupt(void) {
    return 0;
}

void Platform_CommClearInterrupt(void) {
}

uint32_t Platform_GetDeviceMemoryMapXmlSize(void) {
    return sizeof(g_memoryMapXml) - 1;
}

const char* Platform_GetDeviceMemoryMapXml(void) {
    return g_memoryMapXml;
}


const uint8_t* Platform_GetUid(void) {
    return NULL;
}

uint32_t Platform_GetUidSize(void) {
    return 0;
}

int Semihost_IsDebuggeeMakingSemihostCall(void)
{
    return 0;
}

int Semihost_HandleSemihostRequest(void)
{
    return 0;
}




// UNDONE: Just setting manually for now.
#define MRI_DEVICE_HAS_FPU 1

/* NOTE: This is the original version of the following XML which has had things stripped to reduce the amount of
         FLASH consumed by the debug monitor.  This includes the removal of the copyright comment.
<?xml version="1.0"?>
<!-- Copyright (C) 2010, 2011 Free Software Foundation, Inc.

     Copying and distribution of this file, with or without modification,
     are permitted in any medium without royalty provided the copyright
     notice and this notice are preserved.  -->

<!DOCTYPE feature SYSTEM "gdb-target.dtd">
<feature name="org.gnu.gdb.arm.m-profile">
  <reg name="r0" bitsize="32"/>
  <reg name="r1" bitsize="32"/>
  <reg name="r2" bitsize="32"/>
  <reg name="r3" bitsize="32"/>
  <reg name="r4" bitsize="32"/>
  <reg name="r5" bitsize="32"/>
  <reg name="r6" bitsize="32"/>
  <reg name="r7" bitsize="32"/>
  <reg name="r8" bitsize="32"/>
  <reg name="r9" bitsize="32"/>
  <reg name="r10" bitsize="32"/>
  <reg name="r11" bitsize="32"/>
  <reg name="r12" bitsize="32"/>
  <reg name="sp" bitsize="32" type="data_ptr"/>
  <reg name="lr" bitsize="32"/>
  <reg name="pc" bitsize="32" type="code_ptr"/>
  <reg name="xpsr" bitsize="32" regnum="25"/>
</feature>
*/
static const char g_targetXml[] =
    "<?xml version=\"1.0\"?>\n"
    "<!DOCTYPE feature SYSTEM \"gdb-target.dtd\">\n"
    "<target>\n"
    "<feature name=\"org.gnu.gdb.arm.m-profile\">\n"
    "<reg name=\"r0\" bitsize=\"32\"/>\n"
    "<reg name=\"r1\" bitsize=\"32\"/>\n"
    "<reg name=\"r2\" bitsize=\"32\"/>\n"
    "<reg name=\"r3\" bitsize=\"32\"/>\n"
    "<reg name=\"r4\" bitsize=\"32\"/>\n"
    "<reg name=\"r5\" bitsize=\"32\"/>\n"
    "<reg name=\"r6\" bitsize=\"32\"/>\n"
    "<reg name=\"r7\" bitsize=\"32\"/>\n"
    "<reg name=\"r8\" bitsize=\"32\"/>\n"
    "<reg name=\"r9\" bitsize=\"32\"/>\n"
    "<reg name=\"r10\" bitsize=\"32\"/>\n"
    "<reg name=\"r11\" bitsize=\"32\"/>\n"
    "<reg name=\"r12\" bitsize=\"32\"/>\n"
    "<reg name=\"sp\" bitsize=\"32\" type=\"data_ptr\"/>\n"
    "<reg name=\"lr\" bitsize=\"32\"/>\n"
    "<reg name=\"pc\" bitsize=\"32\" type=\"code_ptr\"/>\n"
    "<reg name=\"xpsr\" bitsize=\"32\" regnum=\"25\"/>\n"
    "</feature>\n"
    "<feature name=\"org.gnu.gdb.arm.m-system\">\n"
    "<reg name=\"msp\" bitsize=\"32\" regnum=\"26\"/>\n"
    "<reg name=\"psp\" bitsize=\"32\" regnum=\"27\"/>\n"
    "<reg name=\"primask\" bitsize=\"32\" regnum=\"28\"/>\n"
    "<reg name=\"basepri\" bitsize=\"32\" regnum=\"29\"/>\n"
    "<reg name=\"faultmask\" bitsize=\"32\" regnum=\"30\"/>\n"
    "<reg name=\"control\" bitsize=\"32\" regnum=\"31\"/>\n"
    "</feature>\n"
#if MRI_DEVICE_HAS_FPU
    "<feature name=\"org.gnu.gdb.arm.vfp\">\n"
    "<reg name=\"d0\" bitsize=\"64\" type=\"ieee_double\"/>\n"
    "<reg name=\"d1\" bitsize=\"64\" type=\"ieee_double\"/>\n"
    "<reg name=\"d2\" bitsize=\"64\" type=\"ieee_double\"/>\n"
    "<reg name=\"d3\" bitsize=\"64\" type=\"ieee_double\"/>\n"
    "<reg name=\"d4\" bitsize=\"64\" type=\"ieee_double\"/>\n"
    "<reg name=\"d5\" bitsize=\"64\" type=\"ieee_double\"/>\n"
    "<reg name=\"d6\" bitsize=\"64\" type=\"ieee_double\"/>\n"
    "<reg name=\"d7\" bitsize=\"64\" type=\"ieee_double\"/>\n"
    "<reg name=\"d8\" bitsize=\"64\" type=\"ieee_double\"/>\n"
    "<reg name=\"d9\" bitsize=\"64\" type=\"ieee_double\"/>\n"
    "<reg name=\"d10\" bitsize=\"64\" type=\"ieee_double\"/>\n"
    "<reg name=\"d11\" bitsize=\"64\" type=\"ieee_double\"/>\n"
    "<reg name=\"d12\" bitsize=\"64\" type=\"ieee_double\"/>\n"
    "<reg name=\"d13\" bitsize=\"64\" type=\"ieee_double\"/>\n"
    "<reg name=\"d14\" bitsize=\"64\" type=\"ieee_double\"/>\n"
    "<reg name=\"d15\" bitsize=\"64\" type=\"ieee_double\"/>\n"
    "<reg name=\"fpscr\" bitsize=\"32\" type=\"int\" group=\"float\"/>\n"
    "</feature>\n"
#endif
    "</target>\n";

static struct Context
{
    uint32_t    R0;
    uint32_t    R1;
    uint32_t    R2;
    uint32_t    R3;
    uint32_t    R4;
    uint32_t    R5;
    uint32_t    R6;
    uint32_t    R7;
    uint32_t    R8;
    uint32_t    R9;
    uint32_t    R10;
    uint32_t    R11;
    uint32_t    R12;
    uint32_t    SP;
    uint32_t    LR;
    uint32_t    PC;
    uint32_t    CPSR;
    uint32_t    MSP;
    uint32_t    PSP;
    uint32_t    PRIMASK;
    uint32_t    BASEPRI;
    uint32_t    FAULTMASK;
    uint32_t    CONTROL;
#if MRI_DEVICE_HAS_FPU
    uint32_t    S0;
    uint32_t    S1;
    uint32_t    S2;
    uint32_t    S3;
    uint32_t    S4;
    uint32_t    S5;
    uint32_t    S6;
    uint32_t    S7;
    uint32_t    S8;
    uint32_t    S9;
    uint32_t    S10;
    uint32_t    S11;
    uint32_t    S12;
    uint32_t    S13;
    uint32_t    S14;
    uint32_t    S15;
    uint32_t    S16;
    uint32_t    S17;
    uint32_t    S18;
    uint32_t    S19;
    uint32_t    S20;
    uint32_t    S21;
    uint32_t    S22;
    uint32_t    S23;
    uint32_t    S24;
    uint32_t    S25;
    uint32_t    S26;
    uint32_t    S27;
    uint32_t    S28;
    uint32_t    S29;
    uint32_t    S30;
    uint32_t    S31;
    uint32_t    FPSCR;
#endif
} g_context;

/* NOTE: The largest buffer is required for receiving the 'G' command which receives the contents of the registers from
the debugger as two hex digits per byte.  Also need a character for the 'G' command itself. */
#define CORTEXM_PACKET_BUFFER_SIZE  (1 + 2 * sizeof(Context))

static char g_packetBuffer[CORTEXM_PACKET_BUFFER_SIZE];

void mriCortexMInit(Token* pParameterTokens)
{
}

void Platform_DisableSingleStep(void)
{
}

void Platform_EnableSingleStep(void)
{
}

int Platform_IsSingleStepping(void)
{
    return 0;
}


char* Platform_GetPacketBuffer(void)
{
    return g_packetBuffer;
}


uint32_t Platform_GetPacketBufferSize(void)
{
    return sizeof(g_packetBuffer);
}


uint8_t Platform_DetermineCauseOfException(void)
{
    return SIGSTOP;
}

void Platform_DisplayFaultCauseToGdbConsole(void)
{
}

void Platform_EnteringDebugger(void)
{
}

void Platform_LeavingDebugger(void)
{
}

uint32_t Platform_GetProgramCounter(void)
{
    return g_context.PC;
}


void Platform_SetProgramCounter(uint32_t newPC)
{
    g_context.PC = newPC;
}

void Platform_AdvanceProgramCounterToNextInstruction(void)
{
}

int Platform_WasProgramCounterModifiedByUser(void)
{
    return 0;
}


PlatformInstructionType Platform_TypeOfCurrentInstruction(void)
{
    return MRI_PLATFORM_INSTRUCTION_OTHER;
}

PlatformSemihostParameters Platform_GetSemihostCallParameters(void)
{
    PlatformSemihostParameters parameters;

    memset(&parameters, 0, sizeof(parameters));
    return parameters;
}


void Platform_SetSemihostCallReturnAndErrnoValues(int returnValue, int err)
{
}


int Platform_WasMemoryFaultEncountered(void)
{
    return 0;
}


/* Macro to provide index for specified register in the SContext structure. */
#define CONTEXT_MEMBER_INDEX(MEMBER) (offsetof(Context, MEMBER)/sizeof(uint32_t))


static void sendRegisterForTResponse(Buffer* pBuffer, uint8_t registerOffset, uint32_t registerValue);
static void writeBytesToBufferAsHex(Buffer* pBuffer, void* pBytes, size_t byteCount);
void Platform_WriteTResponseRegistersToBuffer(Buffer* pBuffer)
{
    sendRegisterForTResponse(pBuffer, CONTEXT_MEMBER_INDEX(R7), g_context.R7);
    sendRegisterForTResponse(pBuffer, CONTEXT_MEMBER_INDEX(SP), g_context.SP);
    sendRegisterForTResponse(pBuffer, CONTEXT_MEMBER_INDEX(LR), g_context.LR);
    sendRegisterForTResponse(pBuffer, CONTEXT_MEMBER_INDEX(PC), g_context.PC);
}

static void sendRegisterForTResponse(Buffer* pBuffer, uint8_t registerOffset, uint32_t registerValue)
{
    Buffer_WriteByteAsHex(pBuffer, registerOffset);
    Buffer_WriteChar(pBuffer, ':');
    writeBytesToBufferAsHex(pBuffer, &registerValue, sizeof(registerValue));
    Buffer_WriteChar(pBuffer, ';');
}

static void writeBytesToBufferAsHex(Buffer* pBuffer, void* pBytes, size_t byteCount)
{
    uint8_t* pByte = (uint8_t*)pBytes;
    size_t   i;

    for (i = 0 ; i < byteCount ; i++)
        Buffer_WriteByteAsHex(pBuffer, *pByte++);
}


void Platform_CopyContextToBuffer(Buffer* pBuffer)
{
    writeBytesToBufferAsHex(pBuffer, &g_context, sizeof(g_context));
}


static void readBytesFromBufferAsHex(Buffer* pBuffer, void* pBytes, size_t byteCount);
void Platform_CopyContextFromBuffer(Buffer* pBuffer)
{
    readBytesFromBufferAsHex(pBuffer, &g_context, sizeof(g_context));
}

static void readBytesFromBufferAsHex(Buffer* pBuffer, void* pBytes, size_t byteCount)
{
    uint8_t* pByte = (uint8_t*)pBytes;
    size_t   i;

    for (i = 0 ; i < byteCount; i++)
        *pByte++ = Buffer_ReadByteAsHex(pBuffer);
}

void Platform_SetHardwareBreakpointOfGdbKind(uint32_t address, uint32_t kind)
{
}

void Platform_SetHardwareBreakpoint(uint32_t address)
{
}


void Platform_ClearHardwareBreakpointOfGdbKind(uint32_t address, uint32_t kind)
{
}

void Platform_ClearHardwareBreakpoint(uint32_t address)
{
}

void Platform_SetHardwareWatchpoint(uint32_t address, uint32_t size, PlatformWatchpointType type)
{
}

void Platform_ClearHardwareWatchpoint(uint32_t address, uint32_t size, PlatformWatchpointType type)
{
}

uint32_t Platform_GetTargetXmlSize(void)
{
    return sizeof(g_targetXml) - 1;
}


const char* Platform_GetTargetXml(void)
{
    return g_targetXml;
}
