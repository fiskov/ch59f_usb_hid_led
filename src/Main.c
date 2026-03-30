/********************************** (C) COPYRIGHT *******************************
 * File Name          : Main.c
 * Author             : WCH
 * Version            : V1.1
 * Date               : 2022/01/25
 * Description        : Simulates an HID device
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

#include "CH59x_common.h"

#define DevEP0SIZE    0x40
#define DevEP1SIZE    0x40
// Device descriptor (iProduct=2 -> "TestUSB")
// Offsets: [14]=iManufacturer=0, [15]=iProduct=2, [16]=iSerialNumber=0, [17]=bNumConfigurations=1
const uint8_t MyDevDescr[] = {0x12,0x01,0x10,0x01,0x00,0x00,0x00,DevEP0SIZE,0x3d,0x41,0x07,0x21,0x00,0x00,0x00,0x02,0x00,0x01};
// Configuration descriptor
const uint8_t MyCfgDescr[] = {
    0x09,0x02,0x29,0x00,0x01,0x01,0x04,0xA0,0x23,               // Configuration descriptor
    0x09,0x04,0x00,0x00,0x02,0x03,0x00,0x00,0x05,               // Interface descriptor (HID, class=0x03)
    0x09,0x21,0x00,0x01,0x00,0x01,0x22,0x22,0x00,               // HID descriptor
    0x07,0x05,0x81,0x03,DevEP1SIZE,0x00,0x01,                   // Endpoint descriptor IN EP1
    0x07,0x05,0x01,0x03,DevEP1SIZE,0x00,0x01                    // Endpoint descriptor OUT EP1
};
/* String descriptor table */
// String 0: Language ID (English US = 0x0409)
const uint8_t MyLangDescr[]    = {0x04, 0x03, 0x09, 0x04};
// String 2: Product name "TestUSB" (UTF-16LE)
const uint8_t MyProdDescr[]    = {0x10, 0x03,
                                   'T',0x00,'e',0x00,'s',0x00,'t',0x00,
                                   'U',0x00,'S',0x00,'B',0x00};
/* HID report descriptor */
const uint8_t HIDDescr[] = {  0x06, 0x00,0xff,
                              0x09, 0x01,
                              0xa1, 0x01,                                                   // Collection begin
                              0x09, 0x02,                                                   // Usage Page
                              0x15, 0x00,                                                   // Logical  Minimun
                              0x26, 0x00,0xff,                                              // Logical  Maximun
                              0x75, 0x08,                                                   // Report Size
                              0x95, 0x40,                                                   // Report Counet
                              0x81, 0x06,                                                   // Input
                              0x09, 0x02,                                                   // Usage Page
                              0x15, 0x00,                                                   // Logical  Minimun
                              0x26, 0x00,0xff,                                              // Logical  Maximun
                              0x75, 0x08,                                                   // Report Size
                              0x95, 0x40,                                                   // Report Counet
                              0x91, 0x06,                                                   // Output
                              0xC0};

/**********************************************************/
uint8_t        DevConfig, Ready = 0;
uint8_t        SetupReqCode;
uint16_t       SetupReqLen;
const uint8_t *pDescr;
uint8_t        Report_Value = 0x00;
uint8_t        Idle_Value = 0x00;
uint8_t        USB_SleepStatus = 0x00; /* USB sleep status */

// HID device interrupt upload buffer, upload data of 4 bytes
uint8_t HID_Buf[DevEP1SIZE] = {0,0,0,0};

/******** User-defined endpoint RAM ****************************************/
__attribute__((aligned(4))) uint8_t EP0_Databuf[64 + 64 + 64]; //ep0(64)+ep4_out(64)+ep4_in(64)
__attribute__((aligned(4))) uint8_t EP1_Databuf[64 + 64];      //ep1_out(64)+ep1_in(64)
__attribute__((aligned(4))) uint8_t EP2_Databuf[64 + 64];      //ep2_out(64)+ep2_in(64)
__attribute__((aligned(4))) uint8_t EP3_Databuf[64 + 64];      //ep3_out(64)+ep3_in(64)

/*********************************************************************
 * @fn      USB_DevTransProcess
 *
 * @brief   USB transfer processing function
 *
 * @return  none
 */
void USB_DevTransProcess(void)  // USB device transfer interrupt handler
{
    uint8_t len, chtype;        // len: used for packet length; chtype: used for data transfer direction, request type, and recipient information
    uint8_t intflag, errflag = 0;   // intflag: holds the value of the interrupt flag register; errflag: indicates whether the request is supported

    intflag = R8_USB_INT_FG;        // Read the value of the interrupt flag register

    if(intflag & RB_UIF_TRANSFER)   // Check if the USB transfer completion interrupt flag bit in _INT_FG is set; if so, enter the if block
    {
        if((R8_USB_INT_ST & MASK_UIS_TOKEN) != MASK_UIS_TOKEN) // Not idle   // Check bits 5:4 of the interrupt status register to see the token PID identifier; if both bits are 11 (idle), skip the if block
        {
            switch(R8_USB_INT_ST & (MASK_UIS_TOKEN | MASK_UIS_ENDP))    // Get the token PID identifier and endpoint number (bits 3:0 in device mode; bits 3:0 correspond to PID identifier bits in host mode)
            // Token type and endpoint number
            {                           // EP0 is used for control transfers. The new EP0 IN/OUT transactions correspond to the data phase and status phase of control transfers.
                case UIS_TOKEN_IN:      // Packet PID is IN, bits 5:4 are 10, endpoint number bits 3:0 are 0 (IN control); device sends data to host. _UIS_ = USB interrupt status
                {                       // EP0 is a bidirectional endpoint used for control transfers. (|0 can be omitted)
                    switch(SetupReqCode)// Value set when SETUP packet was received; used in subsequent SETUP request handling, corresponding to the control transfer data phase
                    {
                        case USB_GET_DESCRIPTOR:    // USB standard request: host reads descriptor from USB device
                            len = SetupReqLen >= DevEP0SIZE ? DevEP0SIZE : SetupReqLen; // Packet transfer length; if 64 bytes, send in 64-byte segments; current packet needs to be sent
                            memcpy(pEP0_DataBuf, pDescr, len);// memcpy: memory copy; copies len bytes from source address to destination address
                            // DMA directly reads/writes memory; when data arrives in memory, the chip controller can send the data out; only when the memory and DMA match, DMA can be used
                            SetupReqLen -= len;     // Record the remaining data length to be sent
                            pDescr += len;          // Update the starting address of the data to be sent next time
                            R8_UEP0_T_LEN = len;    // Write the current packet transfer length to EP0 transmit length register
                            R8_UEP0_CTRL ^= RB_UEP_T_TOG;   // Toggle IN direction (for on-chip controller T direction) PID between DATA0 and DATA1
                            break;                  // After writing the control register, the hardware automatically sends the packet with ACK/NAK/STALL; DMA auto-completes the standard packet
                        case USB_SET_ADDRESS:       // USB standard request: assign a unique address (range 0-127; 0 is the default address) to the device
                            R8_USB_DEV_AD = (R8_USB_DEV_AD & RB_UDA_GP_BIT) | SetupReqLen;
                                    // 7-bit address + reserved bit; user-defined address defaults to 1 (bus reset); SetupReqLen holds the address; assigned to the address bits here
                            R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
                                    // R responds to OUT with ACK; T responds to IN with NAK; this CASE does not support IN direction; when DMA has no data in memory, chip has no data to send, respond NAK
                            break;                                                  // Normally after OUT, device sends empty packet; host responds NAK

                        case USB_SET_FEATURE:       // USB standard request: set a feature on a device, interface, or endpoint
                            break;

                        default:
                            R8_UEP0_T_LEN = 0;      // Status phase interrupt or forced upload of 0-length data packet; control transfer (data phase length 0; packet contains only SYNC, PID, EOP fields)
                            R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
                                    // R responds to OUT with ACK; T responds to IN with NAK; this CASE does not support OUT direction; when DMA has data in memory and chip has received data, respond ACK
                            Ready = 1;
                            PRINT("Ready_STATUS = %d\n",Ready);
                            break;
                    }
                }
                break;

                case UIS_TOKEN_OUT:     // Packet PID is OUT, bits 5:4 are 00, endpoint number bits 3:0 are 0 (OUT control); host sends data to device
                {                       // EP0 is a bidirectional endpoint used for control transfers. (|0 can be omitted)
                    len = R8_USB_RX_LEN;    // Read the number of received bytes stored in the current USB receive length register // Receive length register is shared among all endpoints; transmit length register has separate registers
                }
                break;

                case UIS_TOKEN_OUT | 1: // Packet PID is OUT, endpoint number is 1
                {
                    if(R8_USB_INT_ST & RB_UIS_TOG_OK)   // Hardware checks if data toggle is correct; if toggle is correct, the bit auto-clears
                    { // Discard out-of-sync data packets
                        R8_UEP1_CTRL ^= RB_UEP_R_TOG;   // Toggle OUT direction DATA sync; set a specific register value
                        len = R8_USB_RX_LEN;        // Get the number of received data bytes
                        DevEP1_OUT_Deal(len);       // Send len bytes; auto ACK response; user-defined length
                    }
                }
                break;

                case UIS_TOKEN_IN | 1: // Packet PID is IN, endpoint number is 1
                    R8_UEP1_CTRL ^= RB_UEP_T_TOG;       // Toggle IN direction DATA once; set the PID of the packet to be sent
                    R8_UEP1_CTRL = (R8_UEP1_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;    // When DMA has no data ready for the chip to send, set T response to IN request as NAK; no data to send
                    Ready = 1;
                    PRINT("Ready_IN_EP1 = %d\n",Ready);
                    break;
            }
            R8_USB_INT_FG = RB_UIF_TRANSFER;    // Write 1 to clear the interrupt flag
        }

        if(R8_USB_INT_ST & RB_UIS_SETUP_ACT) // Setup packet action
        {
            R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_NAK;
                        // R responds to OUT; next expected DATA1; DMA receives data packet with PID DATA1; if data transfer needs retransmission, ACK; DMA memory receives data; chip receives data
                        // T responds to IN; set to DATA1; chip sends data; DMA memory; send DATA1 out; NAK: chip has no data ready
            SetupReqLen = pSetupReqPak->wLength;    // Number of bytes in data phase      // pSetupReqPak: force-cast EP0 RAM address to a pointer to a setup packet structure; structure members are the request fields
            SetupReqCode = pSetupReqPak->bRequest;  // Request code
            chtype = pSetupReqPak->bRequestType;    // Data transfer direction, request type, and recipient information

            len = 0;
            errflag = 0;
            if((pSetupReqPak->bRequestType & USB_REQ_TYP_MASK) != USB_REQ_TYP_STANDARD) // Check request type; if not standard request, enter if block
            {
                /* Non-standard request */
                /* Vendor request, class request, not standard request */
                if(pSetupReqPak->bRequestType & 0x40)   // Check a specific bit in the request; if not 0, enter if block
                {
                    /* Vendor request */
                }
                else if(pSetupReqPak->bRequestType & 0x20)  // Check a specific bit in the request; if not 0, enter if block
                {   // Determined to be HID class request
                    switch(SetupReqCode)    // Check the request code
                    {
                        case DEF_USB_SET_IDLE: /* 0x0A: SET_IDLE */         // Request to control the idle rate of a specific input report for an HID device
                            Idle_Value = EP0_Databuf[3];
                            break; // One more requirement

                        case DEF_USB_SET_REPORT: /* 0x09: SET_REPORT */     // Request to set the report data of an HID device
                            break;

                        case DEF_USB_SET_PROTOCOL: /* 0x0B: SET_PROTOCOL */ // Request to set the protocol currently used by an HID device
                            Report_Value = EP0_Databuf[2];
                            break;

                        case DEF_USB_GET_IDLE: /* 0x02: GET_IDLE */         // Request to get the current idle rate of a specific input report from an HID device
                            EP0_Databuf[0] = Idle_Value;
                            len = 1;
                            break;

                        case DEF_USB_GET_PROTOCOL: /* 0x03: GET_PROTOCOL */     // Request to get the protocol currently used by an HID device
                            EP0_Databuf[0] = Report_Value;
                            len = 1;
                            break;

                        default:
                            errflag = 0xFF;
                    }
                }
            }
            else    // Determined to be a standard request
            {
                switch(SetupReqCode)    // Check the request code
                {
                    case USB_GET_DESCRIPTOR:    // Standard request: get descriptor
                    {
                        switch(((pSetupReqPak->wValue) >> 8))   // High 8 bits; check if the original high 8 bits are 0 or 1; 1 means device, enter s-case
                        {
                            case USB_DESCR_TYP_DEVICE:  // Different values correspond to different descriptors; device descriptor
                            {
                                pDescr = MyDevDescr;    // Store the device descriptor address in pDescr; standard request will upload it; no break at end of case
                                len = MyDevDescr[0];    // Protocol specifies device descriptor length in first byte; read it and assign to len
                            }
                            break;

                            case USB_DESCR_TYP_CONFIG:  // Configuration descriptor
                            {
                                pDescr = MyCfgDescr;    // Store the configuration descriptor address in pDescr; will be sent later
                                len = MyCfgDescr[2];    // Protocol specifies total length of all descriptor information starting from byte 2 of configuration descriptor
                            }
                            break;

                            case USB_DESCR_TYP_HID:     // HID descriptor; or interface descriptor; wIndex in the structure indicates the interface number (same as interface number)
                                switch((pSetupReqPak->wIndex) & 0xff)       // Get low byte; mask out high byte
                                {
                                    /* Select interface */
                                    case 0:
                                        pDescr = (uint8_t *)(&MyCfgDescr[18]);  // Position of interface 1 HID descriptor in the array
                                        len = 9;
                                        break;

                                    default:
                                        /* Unsupported descriptor type */
                                        errflag = 0xff;
                                        break;
                                }
                                break;

                            case USB_DESCR_TYP_REPORT:  // Report descriptor for the device
                            {
                                if(((pSetupReqPak->wIndex) & 0xff) == 0) // Interface 0 report descriptor
                                {
                                    pDescr = HIDDescr; // Ready to upload
                                    len = sizeof(HIDDescr);
                                }
                                else
                                    len = 0xff; // This device only has 2 interfaces; otherwise this code won't execute
                            }
                            break;

                            case USB_DESCR_TYP_STRING:  // String descriptor for the device
                            {
                                switch((pSetupReqPak->wValue) & 0xff)   // Select string based on wValue
                                {
                                    case 0:             // Language ID descriptor
                                        pDescr = MyLangDescr;
                                        len = sizeof(MyLangDescr);
                                        break;
                                    case 2:             // Product string "TestUSB"
                                        pDescr = MyProdDescr;
                                        len = sizeof(MyProdDescr);
                                        break;
                                    default:
                                        errflag = 0xFF; // Unsupported string descriptor
                                        break;
                                }
                            }
                            break;

                            default:
                                errflag = 0xff;
                                break;
                        }
                        if(SetupReqLen > len)
                            SetupReqLen = len;      // Actual total upload length
                        len = (SetupReqLen >= DevEP0SIZE) ? DevEP0SIZE : SetupReqLen;   // Maximum length is 64 bytes
                        memcpy(pEP0_DataBuf, pDescr, len);  // Copy descriptor
                        pDescr += len;
                    }
                    break;

                    case USB_SET_ADDRESS:       // Standard request: set device address
                        SetupReqLen = (pSetupReqPak->wValue) & 0xff;    // Store the address (low byte, device address) temporarily in SetupReqLen
                        break;                                          // Assign to device address register in control phase

                    case USB_GET_CONFIGURATION: // Standard request: get current device configuration
                        pEP0_DataBuf[0] = DevConfig;    // Put device configuration value into RAM
                        if(SetupReqLen > 1)
                            SetupReqLen = 1;    // Data phase byte count is 1, because DevConfig is only one byte
                        break;

                    case USB_SET_CONFIGURATION: // Standard request: set current device configuration
                        DevConfig = (pSetupReqPak->wValue) & 0xff;  // Get low byte; mask out high byte
                        break;

                    case USB_CLEAR_FEATURE:     // Disable a USB device feature/function; can be for device, interface, or endpoint
                    {
                        if((pSetupReqPak->bRequestType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP) // Check if it is an endpoint request; clear endpoint halt/stall state
                        {
                            switch((pSetupReqPak->wIndex) & 0xff)   // Get low byte; mask out high byte; check endpoint
                            {       // High bit of 16-bit value indicates data transfer direction: 0=OUT, 1=IN; low bits are endpoint number
                                case 0x81:      // Clear _TOG and _T_RES bits; write _NAK to respond to IN with NAK, indicating no data to send
                                    R8_UEP1_CTRL = (R8_UEP1_CTRL & ~(RB_UEP_T_TOG | MASK_UEP_T_RES)) | UEP_T_RES_NAK;
                                    break;
                                case 0x01:      // Clear _TOG and _R_RES bits; write _ACK to respond to OUT with ACK, indicating ready to receive
                                    R8_UEP1_CTRL = (R8_UEP1_CTRL & ~(RB_UEP_R_TOG | MASK_UEP_R_RES)) | UEP_R_RES_ACK;
                                    break;
                                default:
                                    errflag = 0xFF; // Unsupported endpoint
                                    break;
                            }
                        }
                        else if((pSetupReqPak->bRequestType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_DEVICE)  // Check if it is a device request; clear device wakeup
                        {
                            if(pSetupReqPak->wValue == 1)   // Wakeup flag bit is 1
                            {
                                USB_SleepStatus &= ~0x01;   // Clear the bit
                            }
                        }
                        else
                        {
                            errflag = 0xFF;
                        }
                    }
                    break;

                    case USB_SET_FEATURE:       // Enable a USB device feature/function; can be for device, interface, or endpoint
                        if((pSetupReqPak->bRequestType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP) // Check if it is an endpoint request; enable endpoint halt/stall
                        {
                            /* Endpoint */
                            switch(pSetupReqPak->wIndex)    // Check endpoint
                            {       // High bit of 16-bit value indicates data transfer direction: 0=OUT, 1=IN; low bits are endpoint number
                                case 0x81:      // Set _TOG and _T_RES bits; write _STALL to halt the endpoint; stop endpoint operation
                                    R8_UEP1_CTRL = (R8_UEP1_CTRL & ~(RB_UEP_T_TOG | MASK_UEP_T_RES)) | UEP_T_RES_STALL;
                                    break;
                                case 0x01:      // Set _TOG and _R_RES bits; write _STALL to halt the endpoint; stop endpoint operation
                                    R8_UEP1_CTRL = (R8_UEP1_CTRL & ~(RB_UEP_R_TOG | MASK_UEP_R_RES)) | UEP_R_RES_STALL;
                                    break;
                                default:
                                    /* Unsupported endpoint */
                                    errflag = 0xFF; // Unsupported endpoint
                                    break;
                            }
                        }
                        else if((pSetupReqPak->bRequestType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_DEVICE)  // Check if it is a device request; enable device wakeup
                        {
                            if(pSetupReqPak->wValue == 1)
                            {
                                USB_SleepStatus |= 0x01;    // Enable sleep
                            }
                        }
                        else
                        {
                            errflag = 0xFF;
                        }
                        break;

                    case USB_GET_INTERFACE:     // Standard request: get the currently selected alternate setting value for an interface
                        pEP0_DataBuf[0] = 0x00;
                        if(SetupReqLen > 1)
                            SetupReqLen = 1;    // Data phase byte count is 1, because the alternate setting is only one byte
                        break;

                    case USB_SET_INTERFACE:     // Standard request: activate a specific alternate setting for a device interface
                        break;

                    case USB_GET_STATUS:        // Standard request: get the status of a device, interface, or endpoint
                        if((pSetupReqPak->bRequestType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP) // Check if it is endpoint status
                        {
                            /* Endpoint */
                            pEP0_DataBuf[0] = 0x00;
                            switch(pSetupReqPak->wIndex)
                            {       // High bit of 16-bit value indicates data transfer direction: 0=OUT, 1=IN; low bits are endpoint number
                                case 0x81:      // Check _TOG and _T_RES bits; if in STALL state, enter if block
                                    if((R8_UEP1_CTRL & (RB_UEP_T_TOG | MASK_UEP_T_RES)) == UEP_T_RES_STALL)
                                    {
                                        pEP0_DataBuf[0] = 0x01; // Set D0 to 1 to indicate endpoint is halted/stalled; cleared by SET_FEATURE or CLEAR_FEATURE requests
                                    }
                                    break;

                                case 0x01:      // Check _TOG and _R_RES bits; if in STALL state, enter if block
                                    if((R8_UEP1_CTRL & (RB_UEP_R_TOG | MASK_UEP_R_RES)) == UEP_R_RES_STALL)
                                    {
                                        pEP0_DataBuf[0] = 0x01;
                                    }
                                    break;
                            }
                        }
                        else if((pSetupReqPak->bRequestType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_DEVICE)  // Check if it is device status
                        {
                            pEP0_DataBuf[0] = 0x00;
                            if(USB_SleepStatus)     // If device is in sleep state
                            {
                                pEP0_DataBuf[0] = 0x02;     // D0 bit 0 means device is bus-powered; 1 means self-powered. D1 bit 1 means remote wakeup supported; 0 means not supported
                            }
                            else
                            {
                                pEP0_DataBuf[0] = 0x00;
                            }
                        }
                        pEP0_DataBuf[1] = 0;    // Status information format is 16-bit; high byte is always 0
                        if(SetupReqLen >= 2)
                        {
                            SetupReqLen = 2;    // Data phase byte count is 2, because the status is only 2 bytes
                        }
                        break;

                    default:
                        errflag = 0xff;
                        break;
                }
            }
            if(errflag == 0xff) // Request not supported
            {
                //                  SetupReqCode = 0xFF;
                R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_STALL | UEP_T_RES_STALL; // STALL
                Ready = 1;
                PRINT("Ready_Stall = %d\n",Ready);
            }
            else
            {
                if(chtype & 0x80)   // Upload direction bit is 1; data transfer direction is device-to-host upload
                {
                    len = (SetupReqLen > DevEP0SIZE) ? DevEP0SIZE : SetupReqLen;
                    SetupReqLen -= len;
                }
                else
                    len = 0;        // Download direction bit is 0; data transfer direction is host-to-device download
                R8_UEP0_T_LEN = len;
                R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_ACK;     // Default data packet is DATA1
            }

            R8_USB_INT_FG = RB_UIF_TRANSFER;    // Write 1 to clear interrupt flag
        }
    }


    else if(intflag & RB_UIF_BUS_RST)   // Check if the bus reset flag bit in _INT_FG is 1; if so, handle it
    {
        R8_USB_DEV_AD = 0;      // Write 0 to device address; after bus reset, device gets a new address
        R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;   // Write EP0 control register; OUT responds ACK (ready to receive); IN responds NAK (nothing to send)
        R8_UEP1_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
        R8_UEP2_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
        R8_UEP3_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
        R8_USB_INT_FG = RB_UIF_BUS_RST; // Write 1 to clear interrupt flag
    }
    else if(intflag & RB_UIF_SUSPEND)   // Check if the bus suspend/resume event interrupt flag bit in _INT_FG is set; both suspend and wakeup trigger this interrupt
    {
        if(R8_USB_MIS_ST & RB_UMS_SUSPEND)  // Read the suspend status bit in the miscellaneous status register; 1 means USB bus is in suspend state; 0 means bus is active
        {
            Ready = 0;
            PRINT("Ready_Sleep = %d\n",Ready);
        } // Suspend     // If device is in idle state for more than 3ms, it needs to pull down the data lines on the bus
        else    // Wakeup interrupt triggered; bus has not been judged as suspended
        {
            Ready = 1;
            PRINT("Ready_WeakUp = %d\n",Ready);
        } // Wakeup
        R8_USB_INT_FG = RB_UIF_SUSPEND; // Write 1 to clear interrupt flag
    }
    else
    {
        R8_USB_INT_FG = intflag;    // _INT_FG has no interrupt flag; write original value back to original register
    }
}

/*********************************************************************
 * @fn      DevHIDReport
 *
 * @brief   Upload HID report
 *
 * @return  0: success
 *          1: failure
 */
void DevHIDReport(uint8_t data0,uint8_t data1,uint8_t data2,uint8_t data3)
{
    HID_Buf[0] = data0;
    HID_Buf[1] = data1;
    HID_Buf[2] = data2;
    HID_Buf[3] = data3;
    memcpy(pEP1_IN_DataBuf, HID_Buf, sizeof(HID_Buf));
    DevEP1_IN_Deal(DevEP1SIZE);
}

/*********************************************************************
 * @fn      DevWakeup
 *
 * @brief   Device mode remote wakeup
 *
 * @return  none
 */
void DevWakeup(void)
{
    R16_PIN_ANALOG_IE &= ~(RB_PIN_USB_DP_PU);
    R8_UDEV_CTRL |= RB_UD_LOW_SPEED;
    mDelaymS(2);
    R8_UDEV_CTRL &= ~RB_UD_LOW_SPEED;
    R16_PIN_ANALOG_IE |= RB_PIN_USB_DP_PU;
}

/*********************************************************************
 * @fn      DebugInit
 *
 * @brief   Debug initialization
 *
 * @return  none
 */
#define LED_SET() GPIOA_SetBits(GPIO_Pin_4)
#define LED_RESET() GPIOA_ResetBits(GPIO_Pin_4)

void DebugInit(void)
{
    GPIOA_SetBits(GPIO_Pin_9);
    GPIOA_ModeCfg(GPIO_Pin_8, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(GPIO_Pin_9, GPIO_ModeOut_PP_5mA);
    UART1_DefInit();

    GPIOA_ModeCfg(GPIO_Pin_4, GPIO_ModeOut_PP_5mA);
}

/*********************************************************************
 * @fn      main
 *
 * @brief   Main function
 *
 * @return  none
 */
int main()
{
    uint8_t s;
    SetSysClock(CLK_SOURCE_PLL_60MHz);

    DebugInit();        // Initialize UART1 for printf/debug
    printf("\nStart\n");

    pEP0_RAM_Addr = EP0_Databuf;    // User buffer 64 bytes
    pEP1_RAM_Addr = EP1_Databuf;

    USB_DeviceInit();

    PFIC_EnableIRQ(USB_IRQn);       // Enable interrupt
    mDelaymS(100);

    while(1)
    {// Simulate sending 4 bytes of data; actual data content can be modified by the user as needed
        LED_SET();
        if(Ready)
        {
            Ready = 0;
            DevHIDReport(0x05, 0x10, 0x20, 0x11);
        }
        mDelaymS(100);
        LED_RESET();

        if(Ready)
        {
            Ready = 0;
            DevHIDReport(0x0A, 0x15, 0x25, 0x22);
        }
        mDelaymS(100);

        if(Ready)
        {
            Ready = 0;
            DevHIDReport(0x0E, 0x1A, 0x2A, 0x44);
        }
        mDelaymS(100);

        if(Ready)
        {
            Ready = 0;
            DevHIDReport(0x10, 0x1E, 0x2E, 0x88);
        }
        mDelaymS(100);
    }
}

/*********************************************************************
 * @fn      DevEP1_OUT_Deal
 *
 * @brief   EP1 data processing: after receiving data, invert and send back; user can modify this.
 *
 * @return  none
 */
void DevEP1_OUT_Deal(uint8_t l)
{ /* User-defined */
    uint8_t i;

    for(i = 0; i < l; i++)
    {
        pEP1_IN_DataBuf[i] = ~pEP1_OUT_DataBuf[i];
    }
    DevEP1_IN_Deal(l);
}


/*********************************************************************
 * @fn      USB_IRQHandler
 *
 * @brief   USB interrupt handler
 *
 * @return  none
 */
__attribute__((interrupt("WCH-Interrupt-fast")))
__attribute__((section(".highcode")))
void USB_IRQHandler(void) /* USB interrupt service routine, uses register group 1 */
{
    USB_DevTransProcess();
}
