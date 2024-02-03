#ifndef DEV_ACPI_FADT_H
#define DEV_ACPI_FADT_H

#include "acpi.h"

typedef enum addr_space {
    SYSTEM_MEMORY       = 0,
    SYSTEM_IO           = 1,
    PCI_CONFIGURATION   = 2,
    EMBEDDED_CONTROLLER = 3,
    SMBUS               = 4,
    SYSTEM_CMOS         = 5,
    PCI_BAR_TARGET      = 6,
    IPMI                = 7,
    GPIO                = 8,
    GENERIC_SERIAL_BUS  = 9,
    PLATFORM_COMM_CHAN  = 10,
    FUNC_FIXED_HARDWARE = 0x7f,
} addr_space_t;

typedef enum pm_profile {
    PM_UNSPECIFIED  = 0,
    PM_DESKTOP      = 1,
    PM_MOBILE       = 2,
    PM_WORKSTATION  = 3,
    PM_ENTERPRISE_SERVER = 4,
    PM_SOHO_SERVER      = 5,
    PM_APPLIANCE_PC     = 6,
    PM_PERFORMANCE_PC   = 7,
    PM_TABLET           = 8,
} pm_profile_t;

// generic address structure，用于描述寄存器地址
typedef struct generic_addr {
    uint8_t  addr_space_id; // 位于哪个地址空间
    uint8_t  bit_width;     // 位宽，0 表示数据结构而非字段
    uint8_t  bit_offset;
    uint8_t  access_size;   // 指数
    uint64_t address;
} PACKED generic_addr_t;

typedef struct fadt {
    acpi_tbl_t  header;
    uint32_t    firmware_ctrl;  // FACS 地址
    uint32_t    dsdt;
    uint8_t     reserved;   // acpi 1.0 中，该字段为 INT_MODEL
    uint8_t     prefered_pm_profile; // 首选电源管理选项
    uint16_t SCI_Interrupt;
    uint32_t SMI_CommandPort;
    uint8_t  AcpiEnable;
    uint8_t  AcpiDisable;
    uint8_t  S4BIOS_REQ;
    uint8_t  PSTATE_Control;
    uint32_t PM1aEventBlock;
    uint32_t PM1bEventBlock;
    uint32_t PM1aControlBlock;
    uint32_t PM1bControlBlock;
    uint32_t PM2ControlBlock;
    uint32_t PMTimerBlock;
    uint32_t GPE0Block;
    uint32_t GPE1Block;
    uint8_t  PM1EventLength;
    uint8_t  PM1ControlLength;
    uint8_t  PM2ControlLength;
    uint8_t  PMTimerLength;
    uint8_t  GPE0Length;
    uint8_t  GPE1Length;
    uint8_t  GPE1Base;
    uint8_t  CStateControl;
    uint16_t WorstC2Latency;
    uint16_t WorstC3Latency;
    uint16_t FlushSize;
    uint16_t FlushStride;
    uint8_t  DutyOffset;
    uint8_t  DutyWidth;
    uint8_t  DayAlarm;
    uint8_t  MonthAlarm;
    uint8_t  Century;

    // reserved in ACPI 1.0; used since ACPI 2.0+
    uint16_t ia_boot_arch;

    uint8_t  Reserved2;
    uint32_t Flags;

    // 12 byte structure; see below for details
    generic_addr_t ResetReg;

    uint8_t  ResetValue;
    uint8_t  Reserved3[3];

    // 64bit pointers - Available on ACPI 2.0+
    uint64_t                X_FirmwareControl;
    uint64_t                X_Dsdt;

    generic_addr_t X_PM1aEventBlock;
    generic_addr_t X_PM1bEventBlock;
    generic_addr_t X_PM1aControlBlock;
    generic_addr_t X_PM1bControlBlock;
    generic_addr_t X_PM2ControlBlock;
    generic_addr_t X_PMTimerBlock;
    generic_addr_t X_GPE0Block;
    generic_addr_t X_GPE1Block;
} PACKED fadt_t;

#endif // DEV_ACPI_FADT_H
