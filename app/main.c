/********************************** (C) COPYRIGHT *******************************
 * File Name          : Main.c
 * Author             : WCH / modified
 * Version            : V1.1
 * Date               : 2026/04/17
 * Description        : Composite USB device: HID + MSC with tiny FAT16 image
 *******************************************************************************/

#include "CH59x_common.h"

#define DevEP0SIZE                 0x40
#define DevEP1SIZE                 0x40
#define MSC_EP_SIZE                0x40

#define USB_DEVICE_CLASS_MISC      0x00
#define USB_DEVICE_SUBCLASS_IAD    0x00
#define USB_DEVICE_PROTOCOL_IAD    0x00

#define USB_CLASS_HID              0x03
#define USB_CLASS_MASS_STORAGE     0x08
#define USB_SUBCLASS_SCSI          0x06
#define USB_PROTOCOL_BULK_ONLY     0x50

#define MSC_INTERFACE_NUM          0x00
#define HID_INTERFACE_NUM          0x01

#define HID_EP_IN                  0x81
#define HID_EP_OUT                 0x01
#define MSC_EP_OUT                 0x02
#define MSC_EP_IN                  0x82

#define MSC_BLOCK_SIZE             512u
#define MSC_BLOCK_COUNT            32u
#define MSC_DISK_SIZE              (MSC_BLOCK_SIZE * MSC_BLOCK_COUNT)

#define MSC_CBW_SIGNATURE          0x43425355UL
#define MSC_CSW_SIGNATURE          0x53425355UL
#define MSC_CSW_STATUS_PASS        0x00
#define MSC_CSW_STATUS_FAIL        0x01

#define SCSI_CMD_TEST_UNIT_READY   0x00
#define SCSI_CMD_REQUEST_SENSE     0x03
#define SCSI_CMD_INQUIRY           0x12
#define SCSI_CMD_MODE_SENSE_6      0x1A
#define SCSI_CMD_START_STOP_UNIT   0x1B
#define SCSI_CMD_PREVENT_ALLOW     0x1E
#define SCSI_CMD_READ_FORMAT_CAP   0x23
#define SCSI_CMD_READ_CAPACITY_10  0x25
#define SCSI_CMD_READ_10           0x28
#define SCSI_CMD_WRITE_10          0x2A
#define SCSI_CMD_VERIFY_10         0x2F
#define SCSI_CMD_MODE_SENSE_10     0x5A

#define MSC_SENSE_NONE             0x00
#define MSC_SENSE_NOT_READY        0x02
#define MSC_SENSE_ILLEGAL_REQUEST  0x05
#define MSC_SENSE_DATA_PROTECT     0x07

#define MSC_ASC_NONE               0x00
#define MSC_ASC_INVALID_COMMAND    0x20
#define MSC_ASC_INVALID_FIELD      0x24
#define MSC_ASC_WRITE_PROTECTED    0x27
#define MSC_ASC_MEDIUM_NOT_PRESENT 0x3A

#define FAT16_BYTES_PER_SECTOR     512u
#define FAT16_SECTORS_PER_CLUSTER  1u
#define MBR_PARTITION_LBA          1u
#define FAT16_RESERVED_SECTORS     1u
#define FAT16_NUM_FATS             1u
#define FAT16_ROOT_ENTRIES         16u
#define FAT16_SECTORS_PER_FAT      1u
#define FAT16_TOTAL_SECTORS        (MSC_BLOCK_COUNT - MBR_PARTITION_LBA)
#define FAT16_MEDIA_DESCRIPTOR     0xF8u
#define FAT16_HIDDEN_SECTORS       MBR_PARTITION_LBA
#define FAT16_SECTORS_PER_TRACK    1u
#define FAT16_NUM_HEADS            1u
#define FAT16_ROOT_DIR_SECTORS     1u
#define FAT16_FIRST_DATA_SECTOR    (FAT16_RESERVED_SECTORS + FAT16_SECTORS_PER_FAT + FAT16_ROOT_DIR_SECTORS)
#define FAT16_FILE_START_CLUSTER   2u
#define FAT16_FILE_START_SECTOR    (MBR_PARTITION_LBA + FAT16_FIRST_DATA_SECTOR + (FAT16_FILE_START_CLUSTER - 2u))

#define URL_FILE_NAME              "WEBUSB  URL"
#define URL_FILE_EXT               "url"
static const char g_url_file_contents[] = "[InternetShortcut]\r\nURL=https://fiskov.github.io/webusb/index.html\r\n";
#define URL_FILE_SIZE              ((uint32_t)(sizeof(g_url_file_contents) - 1u))

#pragma pack(push, 1)
typedef struct
{
    uint32_t dCBWSignature;
    uint32_t dCBWTag;
    uint32_t dCBWDataTransferLength;
    uint8_t  bmCBWFlags;
    uint8_t  bCBWLUN;
    uint8_t  bCBWCBLength;
    uint8_t  CBWCB[16];
} MSC_CBW;

typedef struct
{
    uint32_t dCSWSignature;
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t  bCSWStatus;
} MSC_CSW;
#pragma pack(pop)

const uint8_t MyDevDescr[] = {
    0x12, 0x01,
    0x10, 0x01,
    0xEF,
    0x02,
    0x01,
    DevEP0SIZE,
    0x48, 0x43,
    0x37, 0x55,
    0x00, 0x01,
    0x01,
    0x02,
    0x03,
    0x01
};

const uint8_t MyCfgDescr[] = {
    0x09, 0x02, 0x48, 0x00, 0x02, 0x01, 0x00, 0x80, 0x32,

    0x08, 0x0B, MSC_INTERFACE_NUM, 0x02, USB_CLASS_MASS_STORAGE, USB_SUBCLASS_SCSI, USB_PROTOCOL_BULK_ONLY, 0x05,
    0x09, 0x04, MSC_INTERFACE_NUM, 0x00, 0x02, USB_CLASS_MASS_STORAGE, USB_SUBCLASS_SCSI, USB_PROTOCOL_BULK_ONLY, 0x05,
    0x07, 0x05, MSC_EP_OUT, 0x02, MSC_EP_SIZE, 0x00, 0x01,
    0x07, 0x05, MSC_EP_IN, 0x02, MSC_EP_SIZE, 0x00, 0x01,

    0x09, 0x04, HID_INTERFACE_NUM, 0x00, 0x02, USB_CLASS_HID, 0x00, 0x00, 0x04,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x22, 0x00,
    0x07, 0x05, HID_EP_IN, 0x03, DevEP1SIZE, 0x00, 0x01,
    0x07, 0x05, HID_EP_OUT, 0x03, DevEP1SIZE, 0x00, 0x01
};

const uint8_t MyLangDescr[] = {0x04, 0x03, 0x09, 0x04};
const uint8_t MyManuDescr[] = {
    0x0A, 0x03,
    'd',0x00,'e',0x00,'m',0x00,'o',0x00
};
const uint8_t MyProdDescr[] = {
    0x2A, 0x03,
    'c',0x00,'h',0x00,'5',0x00,'9',0x00,'2',0x00,' ',0x00,
    'H',0x00,'I',0x00,'D',0x00,'+',0x00,'M',0x00,'S',0x00,'C',0x00,
    ' ',0x00,'d',0x00,'e',0x00,'m',0x00,'o',0x00
};
const uint8_t MySerialDescr[] = {
    0x12, 0x03,
    '0',0x00,'0',0x00,'0',0x00,'1',0x00,'2',0x00,'3',0x00,'4',0x00,'5',0x00
};
const uint8_t MyCfgStrDescr[] = {
    0x1A, 0x03,
    'C',0x00,'o',0x00,'m',0x00,'p',0x00,'o',0x00,'s',0x00,'i',0x00,'t',0x00,
    'e',0x00,' ',0x00,'U',0x00,'S',0x00,'B',0x00
};
const uint8_t MyMscStrDescr[] = {
    0x16, 0x03,
    'U',0x00,'R',0x00,'L',0x00,' ',0x00,'D',0x00,'r',0x00,'i',0x00,'v',0x00,
    'e',0x00
};

const uint8_t HIDDescr[] = {
    0x06, 0x00, 0xff,
    0x09, 0x01,
    0xA1, 0x01,
    0x09, 0x02,
    0x15, 0x00,
    0x26, 0x00, 0xff,
    0x75, 0x08,
    0x95, 0x40,
    0x81, 0x06,
    0x09, 0x02,
    0x15, 0x00,
    0x26, 0x00, 0xff,
    0x75, 0x08,
    0x95, 0x40,
    0x91, 0x06,
    0xC0
};

uint8_t        DevConfig, Ready = 0;
uint8_t        SetupReqCode;
uint16_t       SetupReqLen;
const uint8_t *pDescr;
uint8_t        Report_Value = 0x00;
uint8_t        Idle_Value = 0x00;
uint8_t        USB_SleepStatus = 0x00;
uint8_t        HID_Buf[DevEP1SIZE] = {0, 0, 0, 0};

__attribute__((aligned(4))) uint8_t EP0_Databuf[64 + 64 + 64];
__attribute__((aligned(4))) uint8_t EP1_Databuf[64 + 64];
__attribute__((aligned(4))) uint8_t EP2_Databuf[64 + 64];
__attribute__((aligned(4))) uint8_t EP3_Databuf[64 + 64];

static uint8_t msc_disk[MSC_DISK_SIZE];
static MSC_CBW msc_cbw;
static MSC_CSW msc_csw;
static uint32_t msc_data_offset = 0;
static uint32_t msc_data_length = 0;
static uint32_t msc_lba = 0;
static uint32_t msc_transfer_base = 0;
static uint16_t msc_blocks_remaining = 0;
static uint8_t  msc_expect_data_out = 0;
static uint8_t  msc_send_csw_pending = 0;
static uint8_t  msc_sense_key = MSC_SENSE_NONE;
static uint8_t  msc_sense_asc = MSC_ASC_NONE;
static uint8_t  msc_sense_ascq = 0;

static void msc_continue_read(void);

static void put_le16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void put_le32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16) & 0xFFu);
    dst[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static void put_be32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)((value >> 24) & 0xFFu);
    dst[1] = (uint8_t)((value >> 16) & 0xFFu);
    dst[2] = (uint8_t)((value >> 8) & 0xFFu);
    dst[3] = (uint8_t)(value & 0xFFu);
}

static void msc_set_sense(uint8_t key, uint8_t asc, uint8_t ascq)
{
    msc_sense_key = key;
    msc_sense_asc = asc;
    msc_sense_ascq = ascq;
}

static void msc_send_data(const uint8_t *data, uint8_t len)
{
    memcpy(pEP2_IN_DataBuf, data, len);
    DevEP2_IN_Deal(len);
}

static void msc_send_csw(uint8_t status, uint32_t residue)
{
    msc_csw.dCSWSignature = MSC_CSW_SIGNATURE;
    msc_csw.dCSWTag = msc_cbw.dCBWTag;
    msc_csw.dCSWDataResidue = residue;
    msc_csw.bCSWStatus = status;
    memcpy(pEP2_IN_DataBuf, &msc_csw, sizeof(msc_csw));
    DevEP2_IN_Deal(sizeof(msc_csw));
}

static void msc_prepare_disk(void)
{
    uint8_t *mbr = &msc_disk[0];
    uint8_t *boot = &msc_disk[MSC_BLOCK_SIZE * MBR_PARTITION_LBA];
    uint8_t *fat = &msc_disk[MSC_BLOCK_SIZE * (MBR_PARTITION_LBA + 1u)];
    uint8_t *root = &msc_disk[MSC_BLOCK_SIZE * (MBR_PARTITION_LBA + 2u)];
    uint8_t *data = &msc_disk[MSC_BLOCK_SIZE * FAT16_FILE_START_SECTOR];
    uint32_t i;

    memset(msc_disk, 0, sizeof(msc_disk));

    mbr[446 + 0] = 0x80;
    mbr[446 + 4] = 0x06;
    put_le32(&mbr[446 + 8], MBR_PARTITION_LBA);
    put_le32(&mbr[446 + 12], FAT16_TOTAL_SECTORS);
    mbr[510] = 0x55;
    mbr[511] = 0xAA;

    boot[0] = 0xEB;
    boot[1] = 0x3C;
    boot[2] = 0x90;
    memcpy(&boot[3], "MSDOS5.0", 8);
    put_le16(&boot[11], FAT16_BYTES_PER_SECTOR);
    boot[13] = FAT16_SECTORS_PER_CLUSTER;
    put_le16(&boot[14], FAT16_RESERVED_SECTORS);
    boot[16] = FAT16_NUM_FATS;
    put_le16(&boot[17], FAT16_ROOT_ENTRIES);
    put_le16(&boot[19], FAT16_TOTAL_SECTORS);
    boot[21] = FAT16_MEDIA_DESCRIPTOR;
    put_le16(&boot[22], FAT16_SECTORS_PER_FAT);
    put_le16(&boot[24], FAT16_SECTORS_PER_TRACK);
    put_le16(&boot[26], FAT16_NUM_HEADS);
    put_le32(&boot[28], FAT16_HIDDEN_SECTORS);
    put_le32(&boot[32], 0);
    boot[36] = 0x80;
    boot[38] = 0x29;
    put_le32(&boot[39], 0x12345678UL);
    memcpy(&boot[43], "WEBUSBDRV  ", 11);
    memcpy(&boot[54], "FAT16   ", 8);
    boot[510] = 0x55;
    boot[511] = 0xAA;

    fat[0] = FAT16_MEDIA_DESCRIPTOR;
    fat[1] = 0xFF;
    fat[2] = 0xFF;
    fat[3] = 0xFF;
    fat[4] = 0xFF;
    fat[5] = 0xFF;

    memcpy(&root[0], URL_FILE_NAME, 8);
    memcpy(&root[8], URL_FILE_EXT, 3);
    root[11] = 0x20;
    put_le16(&root[26], FAT16_FILE_START_CLUSTER);
    put_le32(&root[28], URL_FILE_SIZE);

    for(i = 0; i < URL_FILE_SIZE; ++i)
    {
        data[i] = (uint8_t)g_url_file_contents[i];
    }
}

static void msc_reset_state(void)
{
    msc_data_offset = 0;
    msc_data_length = 0;
    msc_lba = 0;
    msc_transfer_base = 0;
    msc_blocks_remaining = 0;
    msc_expect_data_out = 0;
    msc_send_csw_pending = 0;
    msc_set_sense(MSC_SENSE_NONE, MSC_ASC_NONE, 0);
}

static void msc_handle_class_request(uint8_t *errflag)
{
    if((pSetupReqPak->bRequestType & 0x1Fu) != USB_REQ_RECIP_INTERF)
    {
        *errflag = 0xFF;
        return;
    }

    switch(SetupReqCode)
    {
        case 0xFE:
            EP0_Databuf[0] = 0x00;
            pDescr = EP0_Databuf;
            SetupReqLen = 1;
            break;

        case 0xFF:
            msc_reset_state();
            SetupReqLen = 0;
            break;

        default:
            *errflag = 0xFF;
            break;
    }
}

static void msc_handle_scsi_command(void)
{
    uint8_t cmd = msc_cbw.CBWCB[0];
    uint8_t buf[64];
    uint32_t alloc_len;
    uint32_t residue = 0;

    memset(buf, 0, sizeof(buf));

    switch(cmd)
    {
        case SCSI_CMD_INQUIRY:
            buf[0] = 0x00;
            buf[1] = 0x80;
            buf[2] = 0x04;
            buf[3] = 0x02;
            buf[4] = 31;
            memcpy(&buf[8], "DEMO    ", 8);
            memcpy(&buf[16], "WEBUSB DISK     ", 16);
            memcpy(&buf[32], "1.00", 4);
            alloc_len = msc_cbw.CBWCB[4];
            if(alloc_len > 36u)
            {
                alloc_len = 36u;
            }
            msc_send_data(buf, (uint8_t)alloc_len);
            residue = (msc_cbw.dCBWDataTransferLength > alloc_len) ? (msc_cbw.dCBWDataTransferLength - alloc_len) : 0;
            msc_csw.dCSWSignature = MSC_CSW_SIGNATURE;
            msc_csw.dCSWTag = msc_cbw.dCBWTag;
            msc_csw.dCSWDataResidue = residue;
            msc_csw.bCSWStatus = MSC_CSW_STATUS_PASS;
            msc_send_csw_pending = 1;
            break;

        case SCSI_CMD_TEST_UNIT_READY:
            msc_send_csw(MSC_CSW_STATUS_PASS, 0);
            break;

        case SCSI_CMD_REQUEST_SENSE:
            buf[0] = 0x70;
            buf[2] = msc_sense_key;
            buf[7] = 10;
            buf[12] = msc_sense_asc;
            buf[13] = msc_sense_ascq;
            alloc_len = msc_cbw.CBWCB[4];
            if(alloc_len > 18u)
            {
                alloc_len = 18u;
            }
            msc_send_data(buf, (uint8_t)alloc_len);
            residue = (msc_cbw.dCBWDataTransferLength > alloc_len) ? (msc_cbw.dCBWDataTransferLength - alloc_len) : 0;
            msc_csw.dCSWSignature = MSC_CSW_SIGNATURE;
            msc_csw.dCSWTag = msc_cbw.dCBWTag;
            msc_csw.dCSWDataResidue = residue;
            msc_csw.bCSWStatus = MSC_CSW_STATUS_PASS;
            msc_send_csw_pending = 1;
            msc_set_sense(MSC_SENSE_NONE, MSC_ASC_NONE, 0);
            break;

        case SCSI_CMD_MODE_SENSE_6:
            buf[0] = 0x03;
            buf[2] = 0x80;
            buf[3] = 0x00;
            alloc_len = msc_cbw.CBWCB[4];
            if(alloc_len > 4u)
            {
                alloc_len = 4u;
            }
            msc_send_data(buf, (uint8_t)alloc_len);
            residue = (msc_cbw.dCBWDataTransferLength > alloc_len) ? (msc_cbw.dCBWDataTransferLength - alloc_len) : 0;
            msc_csw.dCSWSignature = MSC_CSW_SIGNATURE;
            msc_csw.dCSWTag = msc_cbw.dCBWTag;
            msc_csw.dCSWDataResidue = residue;
            msc_csw.bCSWStatus = MSC_CSW_STATUS_PASS;
            msc_send_csw_pending = 1;
            break;

        case SCSI_CMD_MODE_SENSE_10:
            buf[1] = 0x06;
            buf[3] = 0x80;
            alloc_len = ((uint32_t)msc_cbw.CBWCB[7] << 8) | msc_cbw.CBWCB[8];
            if(alloc_len > 8u)
            {
                alloc_len = 8u;
            }
            msc_send_data(buf, (uint8_t)alloc_len);
            residue = (msc_cbw.dCBWDataTransferLength > alloc_len) ? (msc_cbw.dCBWDataTransferLength - alloc_len) : 0;
            msc_csw.dCSWSignature = MSC_CSW_SIGNATURE;
            msc_csw.dCSWTag = msc_cbw.dCBWTag;
            msc_csw.dCSWDataResidue = residue;
            msc_csw.bCSWStatus = MSC_CSW_STATUS_PASS;
            msc_send_csw_pending = 1;
            break;

        case SCSI_CMD_READ_FORMAT_CAP:
            put_be32(&buf[3], MSC_BLOCK_COUNT);
            buf[8] = 0x02;
            put_be32(&buf[9], MSC_BLOCK_SIZE);
            msc_send_data(buf, 12);
            residue = (msc_cbw.dCBWDataTransferLength > 12u) ? (msc_cbw.dCBWDataTransferLength - 12u) : 0;
            msc_csw.dCSWSignature = MSC_CSW_SIGNATURE;
            msc_csw.dCSWTag = msc_cbw.dCBWTag;
            msc_csw.dCSWDataResidue = residue;
            msc_csw.bCSWStatus = MSC_CSW_STATUS_PASS;
            msc_send_csw_pending = 1;
            break;

        case SCSI_CMD_READ_CAPACITY_10:
            put_be32(&buf[0], MSC_BLOCK_COUNT - 1u);
            put_be32(&buf[4], MSC_BLOCK_SIZE);
            msc_send_data(buf, 8);
            residue = (msc_cbw.dCBWDataTransferLength > 8u) ? (msc_cbw.dCBWDataTransferLength - 8u) : 0;
            msc_csw.dCSWSignature = MSC_CSW_SIGNATURE;
            msc_csw.dCSWTag = msc_cbw.dCBWTag;
            msc_csw.dCSWDataResidue = residue;
            msc_csw.bCSWStatus = MSC_CSW_STATUS_PASS;
            msc_send_csw_pending = 1;
            break;

        case SCSI_CMD_READ_10:
            msc_lba = ((uint32_t)msc_cbw.CBWCB[2] << 24) |
                      ((uint32_t)msc_cbw.CBWCB[3] << 16) |
                      ((uint32_t)msc_cbw.CBWCB[4] << 8) |
                      ((uint32_t)msc_cbw.CBWCB[5]);
            msc_blocks_remaining = (uint16_t)(((uint16_t)msc_cbw.CBWCB[7] << 8) | msc_cbw.CBWCB[8]);
            if((msc_lba + msc_blocks_remaining) > MSC_BLOCK_COUNT)
            {
                msc_set_sense(MSC_SENSE_ILLEGAL_REQUEST, MSC_ASC_INVALID_FIELD, 0);
                msc_send_csw(MSC_CSW_STATUS_FAIL, msc_cbw.dCBWDataTransferLength);
                break;
            }
            if(msc_blocks_remaining == 0u)
            {
                msc_send_csw(MSC_CSW_STATUS_PASS, 0);
                break;
            }
            msc_transfer_base = msc_lba * MSC_BLOCK_SIZE;
            msc_data_offset = 0;
            msc_data_length = (uint32_t)msc_blocks_remaining * MSC_BLOCK_SIZE;
            msc_continue_read();
            break;

        case SCSI_CMD_WRITE_10:
            msc_lba = ((uint32_t)msc_cbw.CBWCB[2] << 24) |
                      ((uint32_t)msc_cbw.CBWCB[3] << 16) |
                      ((uint32_t)msc_cbw.CBWCB[4] << 8) |
                      ((uint32_t)msc_cbw.CBWCB[5]);
            msc_blocks_remaining = (uint16_t)(((uint16_t)msc_cbw.CBWCB[7] << 8) | msc_cbw.CBWCB[8]);
            if((msc_lba + msc_blocks_remaining) > MSC_BLOCK_COUNT)
            {
                msc_set_sense(MSC_SENSE_ILLEGAL_REQUEST, MSC_ASC_INVALID_FIELD, 0);
                msc_send_csw(MSC_CSW_STATUS_FAIL, msc_cbw.dCBWDataTransferLength);
                break;
            }
            msc_set_sense(MSC_SENSE_DATA_PROTECT, MSC_ASC_WRITE_PROTECTED, 0);
            msc_send_csw(MSC_CSW_STATUS_FAIL, msc_cbw.dCBWDataTransferLength);
            break;

        case SCSI_CMD_VERIFY_10:
        case SCSI_CMD_START_STOP_UNIT:
        case SCSI_CMD_PREVENT_ALLOW:
            msc_send_csw(MSC_CSW_STATUS_PASS, 0);
            break;

        default:
            msc_set_sense(MSC_SENSE_ILLEGAL_REQUEST, MSC_ASC_INVALID_COMMAND, 0);
            msc_send_csw(MSC_CSW_STATUS_FAIL, msc_cbw.dCBWDataTransferLength);
            break;
    }
}

static void msc_continue_read(void)
{
    uint32_t remaining;
    uint8_t packet_len;

    if(msc_data_length == 0u)
    {
        msc_send_csw(MSC_CSW_STATUS_PASS, 0);
        return;
    }

    remaining = msc_data_length - msc_data_offset;
    if(remaining == 0u)
    {
        msc_data_length = 0;
        msc_send_csw(MSC_CSW_STATUS_PASS, 0);
        return;
    }

    packet_len = (remaining >= MSC_EP_SIZE) ? MSC_EP_SIZE : (uint8_t)remaining;
    memcpy(pEP2_IN_DataBuf, &msc_disk[msc_transfer_base + msc_data_offset], packet_len);
    DevEP2_IN_Deal(packet_len);
    msc_data_offset += packet_len;
}

void USB_DevTransProcess(void)
{
    uint8_t len, chtype;
    uint8_t intflag, errflag = 0;

    intflag = R8_USB_INT_FG;

    if(intflag & RB_UIF_TRANSFER)
    {
        if((R8_USB_INT_ST & MASK_UIS_TOKEN) != MASK_UIS_TOKEN)
        {
            switch(R8_USB_INT_ST & (MASK_UIS_TOKEN | MASK_UIS_ENDP))
            {
                case UIS_TOKEN_IN:
                    switch(SetupReqCode)
                    {
                        case USB_GET_DESCRIPTOR:
                            len = SetupReqLen >= DevEP0SIZE ? DevEP0SIZE : SetupReqLen;
                            memcpy(pEP0_DataBuf, pDescr, len);
                            SetupReqLen -= len;
                            pDescr += len;
                            R8_UEP0_T_LEN = len;
                            R8_UEP0_CTRL ^= RB_UEP_T_TOG;
                            break;

                        case USB_SET_ADDRESS:
                            R8_USB_DEV_AD = (R8_USB_DEV_AD & RB_UDA_GP_BIT) | SetupReqLen;
                            R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
                            break;

                        default:
                            R8_UEP0_T_LEN = 0;
                            R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
                            Ready = 1;
                            break;
                    }
                    break;

                case UIS_TOKEN_OUT:
                    len = R8_USB_RX_LEN;
                    (void)len;
                    break;

                case UIS_TOKEN_OUT | 1:
                    if(R8_USB_INT_ST & RB_UIS_TOG_OK)
                    {
                        R8_UEP1_CTRL ^= RB_UEP_R_TOG;
                        len = R8_USB_RX_LEN;
                        DevEP1_OUT_Deal(len);
                    }
                    break;

                case UIS_TOKEN_IN | 1:
                    R8_UEP1_CTRL ^= RB_UEP_T_TOG;
                    R8_UEP1_CTRL = (R8_UEP1_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
                    Ready = 1;
                    break;

                case UIS_TOKEN_OUT | 2:
                    if(R8_USB_INT_ST & RB_UIS_TOG_OK)
                    {
                        R8_UEP2_CTRL ^= RB_UEP_R_TOG;
                        len = R8_USB_RX_LEN;
                        if(msc_expect_data_out)
                        {
                            msc_expect_data_out = 0;
                            msc_set_sense(MSC_SENSE_DATA_PROTECT, MSC_ASC_WRITE_PROTECTED, 0);
                            msc_send_csw(MSC_CSW_STATUS_FAIL, msc_cbw.dCBWDataTransferLength);
                        }
                        else if(len == sizeof(MSC_CBW))
                        {
                            memcpy(&msc_cbw, pEP2_OUT_DataBuf, sizeof(MSC_CBW));
                            if(msc_cbw.dCBWSignature != MSC_CBW_SIGNATURE)
                            {
                                msc_set_sense(MSC_SENSE_ILLEGAL_REQUEST, MSC_ASC_INVALID_FIELD, 0);
                                msc_send_csw(MSC_CSW_STATUS_FAIL, 0);
                            }
                            else
                            {
                                msc_handle_scsi_command();
                            }
                        }
                    }
                    break;

                case UIS_TOKEN_IN | 2:
                    R8_UEP2_CTRL ^= RB_UEP_T_TOG;
                    R8_UEP2_CTRL = (R8_UEP2_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
                    if(msc_data_length != 0u && msc_data_offset < msc_data_length)
                    {
                        msc_continue_read();
                    }
                    else if(msc_data_length != 0u && msc_data_offset >= msc_data_length)
                    {
                        msc_data_length = 0;
                        msc_send_csw(MSC_CSW_STATUS_PASS, 0);
                    }
                    else if(msc_send_csw_pending)
                    {
                        msc_send_csw_pending = 0;
                        memcpy(pEP2_IN_DataBuf, &msc_csw, sizeof(msc_csw));
                        DevEP2_IN_Deal(sizeof(msc_csw));
                    }
                    break;
            }
            R8_USB_INT_FG = RB_UIF_TRANSFER;
        }

        if(R8_USB_INT_ST & RB_UIS_SETUP_ACT)
        {
            R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_NAK;
            SetupReqLen = pSetupReqPak->wLength;
            SetupReqCode = pSetupReqPak->bRequest;
            chtype = pSetupReqPak->bRequestType;

            len = 0;
            errflag = 0;
            if((pSetupReqPak->bRequestType & USB_REQ_TYP_MASK) != USB_REQ_TYP_STANDARD)
            {
                if(pSetupReqPak->bRequestType & 0x20)
                {
                    if((pSetupReqPak->bRequestType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_INTERF &&
                       ((pSetupReqPak->wIndex & 0xFFu) == HID_INTERFACE_NUM))
                    {
                        switch(SetupReqCode)
                        {
                            case DEF_USB_SET_IDLE:
                                Idle_Value = EP0_Databuf[3];
                                break;

                            case DEF_USB_SET_REPORT:
                                break;

                            case DEF_USB_SET_PROTOCOL:
                                Report_Value = EP0_Databuf[2];
                                break;

                            case DEF_USB_GET_IDLE:
                                EP0_Databuf[0] = Idle_Value;
                                pDescr = EP0_Databuf;
                                len = 1;
                                break;

                            case DEF_USB_GET_PROTOCOL:
                                EP0_Databuf[0] = Report_Value;
                                pDescr = EP0_Databuf;
                                len = 1;
                                break;

                            default:
                                msc_handle_class_request(&errflag);
                                break;
                        }
                    }
                    else if((pSetupReqPak->wIndex & 0xFFu) == MSC_INTERFACE_NUM)
                    {
                        msc_handle_class_request(&errflag);
                    }
                    else
                    {
                        errflag = 0xFF;
                    }
                }
                else
                {
                    errflag = 0xFF;
                }
            }
            else
            {
                switch(SetupReqCode)
                {
                    case USB_GET_DESCRIPTOR:
                        switch(((pSetupReqPak->wValue) >> 8))
                        {
                            case USB_DESCR_TYP_DEVICE:
                                pDescr = MyDevDescr;
                                len = sizeof(MyDevDescr);
                                break;

                            case USB_DESCR_TYP_CONFIG:
                                pDescr = MyCfgDescr;
                                len = sizeof(MyCfgDescr);
                                break;

                            case USB_DESCR_TYP_HID:
                                if(((pSetupReqPak->wIndex) & 0xFFu) == HID_INTERFACE_NUM)
                                {
                                    pDescr = (uint8_t *)(&MyCfgDescr[26]);
                                    len = 9;
                                }
                                else
                                {
                                    errflag = 0xFF;
                                }
                                break;

                            case USB_DESCR_TYP_REPORT:
                                if(((pSetupReqPak->wIndex) & 0xFFu) == HID_INTERFACE_NUM)
                                {
                                    pDescr = HIDDescr;
                                    len = sizeof(HIDDescr);
                                }
                                else
                                {
                                    errflag = 0xFF;
                                }
                                break;

                            case USB_DESCR_TYP_STRING:
                                switch((pSetupReqPak->wValue) & 0xFFu)
                                {
                                    case 0:
                                        pDescr = MyLangDescr;
                                        len = sizeof(MyLangDescr);
                                        break;
                                    case 1:
                                        pDescr = MyManuDescr;
                                        len = sizeof(MyManuDescr);
                                        break;
                                    case 2:
                                        pDescr = MyProdDescr;
                                        len = sizeof(MyProdDescr);
                                        break;
                                    case 3:
                                        pDescr = MySerialDescr;
                                        len = sizeof(MySerialDescr);
                                        break;
                                    case 4:
                                        pDescr = MyCfgStrDescr;
                                        len = sizeof(MyCfgStrDescr);
                                        break;
                                    case 5:
                                        pDescr = MyMscStrDescr;
                                        len = sizeof(MyMscStrDescr);
                                        break;
                                    default:
                                        errflag = 0xFF;
                                        break;
                                }
                                break;

                            default:
                                errflag = 0xFF;
                                break;
                        }
                        if(errflag == 0)
                        {
                            if(SetupReqLen > len)
                            {
                                SetupReqLen = len;
                            }
                            len = (SetupReqLen >= DevEP0SIZE) ? DevEP0SIZE : SetupReqLen;
                            memcpy(pEP0_DataBuf, pDescr, len);
                            pDescr += len;
                        }
                        break;

                    case USB_SET_ADDRESS:
                        SetupReqLen = (pSetupReqPak->wValue) & 0xFFu;
                        break;

                    case USB_GET_CONFIGURATION:
                        pEP0_DataBuf[0] = DevConfig;
                        pDescr = pEP0_DataBuf;
                        if(SetupReqLen > 1)
                        {
                            SetupReqLen = 1;
                        }
                        break;

                    case USB_SET_CONFIGURATION:
                        DevConfig = (pSetupReqPak->wValue) & 0xFFu;
                        break;

                    case USB_CLEAR_FEATURE:
                        if((pSetupReqPak->bRequestType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP)
                        {
                            switch((pSetupReqPak->wIndex) & 0xFFu)
                            {
                                case HID_EP_IN:
                                    R8_UEP1_CTRL = (R8_UEP1_CTRL & ~(RB_UEP_T_TOG | MASK_UEP_T_RES)) | UEP_T_RES_NAK;
                                    break;
                                case HID_EP_OUT:
                                    R8_UEP1_CTRL = (R8_UEP1_CTRL & ~(RB_UEP_R_TOG | MASK_UEP_R_RES)) | UEP_R_RES_ACK;
                                    break;
                                case MSC_EP_IN:
                                    R8_UEP2_CTRL = (R8_UEP2_CTRL & ~(RB_UEP_T_TOG | MASK_UEP_T_RES)) | UEP_T_RES_NAK;
                                    break;
                                case MSC_EP_OUT:
                                    R8_UEP2_CTRL = (R8_UEP2_CTRL & ~(RB_UEP_R_TOG | MASK_UEP_R_RES)) | UEP_R_RES_ACK;
                                    break;
                                default:
                                    errflag = 0xFF;
                                    break;
                            }
                        }
                        else if((pSetupReqPak->bRequestType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_DEVICE)
                        {
                            if(pSetupReqPak->wValue == 1)
                            {
                                USB_SleepStatus &= (uint8_t)~0x01u;
                            }
                        }
                        else
                        {
                            errflag = 0xFF;
                        }
                        break;

                    case USB_SET_FEATURE:
                        if((pSetupReqPak->bRequestType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP)
                        {
                            switch(pSetupReqPak->wIndex & 0xFFu)
                            {
                                case HID_EP_IN:
                                    R8_UEP1_CTRL = (R8_UEP1_CTRL & ~(RB_UEP_T_TOG | MASK_UEP_T_RES)) | UEP_T_RES_STALL;
                                    break;
                                case HID_EP_OUT:
                                    R8_UEP1_CTRL = (R8_UEP1_CTRL & ~(RB_UEP_R_TOG | MASK_UEP_R_RES)) | UEP_R_RES_STALL;
                                    break;
                                case MSC_EP_IN:
                                    R8_UEP2_CTRL = (R8_UEP2_CTRL & ~(RB_UEP_T_TOG | MASK_UEP_T_RES)) | UEP_T_RES_STALL;
                                    break;
                                case MSC_EP_OUT:
                                    R8_UEP2_CTRL = (R8_UEP2_CTRL & ~(RB_UEP_R_TOG | MASK_UEP_R_RES)) | UEP_R_RES_STALL;
                                    break;
                                default:
                                    errflag = 0xFF;
                                    break;
                            }
                        }
                        else if((pSetupReqPak->bRequestType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_DEVICE)
                        {
                            if(pSetupReqPak->wValue == 1)
                            {
                                USB_SleepStatus |= 0x01;
                            }
                        }
                        else
                        {
                            errflag = 0xFF;
                        }
                        break;

                    case USB_GET_INTERFACE:
                        pEP0_DataBuf[0] = 0x00;
                        pDescr = pEP0_DataBuf;
                        if(SetupReqLen > 1)
                        {
                            SetupReqLen = 1;
                        }
                        break;

                    case USB_SET_INTERFACE:
                        break;

                    case USB_GET_STATUS:
                        pEP0_DataBuf[0] = 0x00;
                        pEP0_DataBuf[1] = 0x00;
                        if((pSetupReqPak->bRequestType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP)
                        {
                            switch(pSetupReqPak->wIndex & 0xFFu)
                            {
                                case HID_EP_IN:
                                    if((R8_UEP1_CTRL & MASK_UEP_T_RES) == UEP_T_RES_STALL)
                                    {
                                        pEP0_DataBuf[0] = 0x01;
                                    }
                                    break;
                                case HID_EP_OUT:
                                    if((R8_UEP1_CTRL & MASK_UEP_R_RES) == UEP_R_RES_STALL)
                                    {
                                        pEP0_DataBuf[0] = 0x01;
                                    }
                                    break;
                                case MSC_EP_IN:
                                    if((R8_UEP2_CTRL & MASK_UEP_T_RES) == UEP_T_RES_STALL)
                                    {
                                        pEP0_DataBuf[0] = 0x01;
                                    }
                                    break;
                                case MSC_EP_OUT:
                                    if((R8_UEP2_CTRL & MASK_UEP_R_RES) == UEP_R_RES_STALL)
                                    {
                                        pEP0_DataBuf[0] = 0x01;
                                    }
                                    break;
                                default:
                                    errflag = 0xFF;
                                    break;
                            }
                        }
                        else if((pSetupReqPak->bRequestType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_DEVICE)
                        {
                            if(USB_SleepStatus)
                            {
                                pEP0_DataBuf[0] = 0x02;
                            }
                        }
                        else if((pSetupReqPak->bRequestType & USB_REQ_RECIP_MASK) != USB_REQ_RECIP_INTERF)
                        {
                            errflag = 0xFF;
                        }
                        if(SetupReqLen >= 2)
                        {
                            SetupReqLen = 2;
                        }
                        pDescr = pEP0_DataBuf;
                        break;

                    default:
                        errflag = 0xFF;
                        break;
                }
            }

            if(errflag == 0xFF)
            {
                R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_STALL | UEP_T_RES_STALL;
                Ready = 1;
            }
            else
            {
                if(chtype & 0x80)
                {
                    len = (SetupReqLen > DevEP0SIZE) ? DevEP0SIZE : SetupReqLen;
                    SetupReqLen -= len;
                }
                else
                {
                    len = 0;
                }
                R8_UEP0_T_LEN = len;
                R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_ACK;
            }

            R8_USB_INT_FG = RB_UIF_TRANSFER;
        }
    }
    else if(intflag & RB_UIF_BUS_RST)
    {
        R8_USB_DEV_AD = 0;
        R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
        R8_UEP1_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
        R8_UEP2_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
        R8_UEP3_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
        R8_USB_INT_FG = RB_UIF_BUS_RST;
        DevConfig = 0;
        Ready = 0;
        msc_reset_state();
    }
    else if(intflag & RB_UIF_SUSPEND)
    {
        if(R8_USB_MIS_ST & RB_UMS_SUSPEND)
        {
            Ready = 0;
        }
        else
        {
            Ready = 1;
        }
        R8_USB_INT_FG = RB_UIF_SUSPEND;
    }
    else
    {
        R8_USB_INT_FG = intflag;
    }
}

void DevHIDReport(uint8_t data0, uint8_t data1, uint8_t data2, uint8_t data3)
{
    HID_Buf[0] = data0;
    HID_Buf[1] = data1;
    HID_Buf[2] = data2;
    HID_Buf[3] = data3;
    memcpy(pEP1_IN_DataBuf, HID_Buf, sizeof(HID_Buf));
    DevEP1_IN_Deal(DevEP1SIZE);
}

void DevWakeup(void)
{
    R16_PIN_ANALOG_IE &= ~(RB_PIN_USB_DP_PU);
    R8_UDEV_CTRL |= RB_UD_LOW_SPEED;
    mDelaymS(2);
    R8_UDEV_CTRL &= ~RB_UD_LOW_SPEED;
    R16_PIN_ANALOG_IE |= RB_PIN_USB_DP_PU;
}

#define LED_RESET() GPIOA_SetBits(GPIO_Pin_4)
#define LED_SET()   GPIOA_ResetBits(GPIO_Pin_4)

void DebugInit(void)
{
    GPIOA_SetBits(GPIO_Pin_9);
    GPIOA_ModeCfg(GPIO_Pin_8, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(GPIO_Pin_9, GPIO_ModeOut_PP_5mA);
    UART1_DefInit();

    GPIOA_ModeCfg(GPIO_Pin_4, GPIO_ModeOut_PP_5mA);
    LED_RESET();
}

int main()
{
    SetSysClock(CLK_SOURCE_PLL_60MHz);

    DebugInit();
    printf("\nStart composite HID+MSC\n");

    pEP0_RAM_Addr = EP0_Databuf;
    pEP1_RAM_Addr = EP1_Databuf;
    pEP2_RAM_Addr = EP2_Databuf;
    pEP3_RAM_Addr = EP3_Databuf;

    msc_prepare_disk();
    msc_reset_state();

    USB_DeviceInit();

    PFIC_EnableIRQ(USB_IRQn);
    mDelaymS(100);

    while(1)
    {
        if(Ready)
        {
            Ready = 0;
            DevHIDReport(0x05, 0x10, 0x20, 0x11);
        }
        mDelaymS(100);

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

void DevEP1_OUT_Deal(uint8_t l)
{
    uint8_t i;

    if(pEP1_OUT_DataBuf[0] & 1u)
    {
        LED_SET();
    }
    else
    {
        LED_RESET();
    }

    for(i = 0; i < l; i++)
    {
        pEP1_IN_DataBuf[i] = (uint8_t)~pEP1_OUT_DataBuf[i];
    }
    DevEP1_IN_Deal(l);
}

__attribute__((interrupt("WCH-Interrupt-fast")))
__attribute__((section(".highcode")))
void USB_IRQHandler(void)
{
    USB_DevTransProcess();
}
